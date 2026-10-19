"""VMU e VMS para o oraculo do N4 (ferramenta de desenvolvimento; docs/N4-PREPARACAO.md).

Independente do jogo e do Flycast: le e prepara imagens de VMU (128 KB, 256 blocos de 512 bytes) e
valida arquivos VMS de save pelo formato observado no binario (8C08CA5C: cabecalho de 128 bytes,
icone de 512, payload, zeros ate o multiplo de 512; CRC-16 0x1021, estado inicial 0, MSB primeiro,
sobre os bytes sem o preenchimento, com o campo 0x46 zerado). As amostras de teste sao sinteticas.

    python -I tools/dev/n4_vmu.py info <vmu.bin>
    python -I tools/dev/n4_vmu.py extrai <vmu.bin> <pasta>
    python -I tools/dev/n4_vmu.py valida <arquivo.vms> [--payload <bin>] [--tamanho 1200] [--bluestinger] [--json <saida>]
    python -I tools/dev/n4_vmu.py nova <vmu.bin>                       VMU formatada e vazia
    python -I tools/dev/n4_vmu.py prepara <entrada> <saida> (--livres N | --zerada | --adultera-crc NOME)
    python -I tools/dev/n4_vmu.py sintetico <saida.vms> [--semente N]  VMS sintetico valido
    python -I tools/dev/n4_vmu.py teste                                autoteste (so amostras sinteticas)
"""
import argparse
import hashlib
import json
import os
import random
import struct
import sys

BLOCO = 512
BLOCOS = 256
RAIZ, FAT, DIR, DIR_N, USUARIO = 255, 254, 253, 13, 200
LIVRE, FIM = 0xFFFC, 0xFFFA
CAB, ICONE = 0x80, 0x200  # cabecalho e um icone (4 bits por pixel, 32x32)


def crc16(data: bytes) -> int:
    """CRC do 8C08CDE2: polinomio 0x1021, inicio 0, MSB primeiro (CRC-16/XMODEM)."""
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
            c &= 0xFFFF
    return c


# ---------------- VMU (sistema de arquivos padrao) ----------------

class Vmu:
    def __init__(self, data: bytes):
        if len(data) != BLOCOS * BLOCO:
            raise ValueError(f"VMU com {len(data)} bytes (esperado {BLOCOS * BLOCO})")
        self.d = bytearray(data)

    def bloco(self, n):
        return self.d[n * BLOCO:(n + 1) * BLOCO]

    def raiz(self):
        r = self.bloco(RAIZ)
        fat, fat_n, dir_, dir_n, _icone, usuario = struct.unpack_from("<HHHHHH", r, 0x46)
        return {"formatada": r[:16] == b"\x55" * 16, "fat": fat, "fat_n": fat_n, "dir": dir_, "dir_n": dir_n,
                "usuario": usuario}

    def fat(self):
        f = self.raiz()["fat"]
        return list(struct.unpack_from("<256H", self.d, f * BLOCO))

    def set_fat(self, i, v):
        struct.pack_into("<H", self.d, self.raiz()["fat"] * BLOCO + 2 * i, v)

    def entradas(self):
        """(indice global, deslocamento, campos) de cada entrada em uso do diretorio."""
        r = self.raiz()
        out = []
        for k in range(r["dir_n"]):
            base = (r["dir"] - k) * BLOCO
            for e in range(BLOCO // 32):
                o = base + e * 32
                t = self.d[o]
                if t in (0x33, 0xCC):
                    out.append((k * 16 + e, o, {
                        "tipo": "dados" if t == 0x33 else "jogo",
                        "nome": bytes(self.d[o + 4:o + 16]).rstrip(b"\0 ").decode("ascii", "replace"),
                        "primeiro": struct.unpack_from("<H", self.d, o + 2)[0],
                        "blocos": struct.unpack_from("<H", self.d, o + 0x18)[0],
                        "cabecalho": struct.unpack_from("<H", self.d, o + 0x1A)[0],
                    }))
        return out

    def livres(self):
        return sum(1 for x in self.fat()[:self.raiz()["usuario"]] if x == LIVRE)

    def le(self, nome):
        for _, _, e in self.entradas():
            if e["nome"] == nome:
                fat, b, out = self.fat(), e["primeiro"], bytearray()
                for i in range(e["blocos"]):
                    if not 0 <= b < BLOCOS:
                        raise ValueError(f"{nome}: cadeia da FAT quebrada no bloco {i}")
                    out += self.bloco(b)
                    b = fat[b]
                if b != FIM:
                    raise ValueError(f"{nome}: cadeia da FAT nao termina em {e['blocos']} blocos ({b:04X})")
                return bytes(out)
        raise KeyError(nome)

    def grava(self, nome, data: bytes, tipo=0x33):
        """Arquivo de dados novo, blocos de cima para baixo (como o SDK aloca dados)."""
        n = -(-len(data) // BLOCO)
        fat = self.fat()
        livres = [i for i in range(self.raiz()["usuario"] - 1, -1, -1) if fat[i] == LIVRE][:n]
        if len(livres) < n:
            raise ValueError("sem blocos livres")
        r = self.raiz()
        slot = None
        for k in range(r["dir_n"]):
            for e in range(16):
                o = (r["dir"] - k) * BLOCO + e * 32
                if self.d[o] == 0:
                    slot = o
                    break
            if slot is not None:
                break
        if slot is None:
            raise ValueError("diretorio cheio")
        data = data + b"\0" * (n * BLOCO - len(data))
        for i, b in enumerate(livres):
            self.d[b * BLOCO:(b + 1) * BLOCO] = data[i * BLOCO:(i + 1) * BLOCO]
            self.set_fat(b, livres[i + 1] if i + 1 < n else FIM)
        e = bytearray(32)
        e[0], e[1] = tipo, 0
        struct.pack_into("<H", e, 2, livres[0])
        e[4:16] = nome.encode("ascii").ljust(12, b"\0")[:12]
        e[0x10:0x18] = bytes.fromhex("2026101900000001")  # data fixa (BCD), reprodutivel
        struct.pack_into("<HH", e, 0x18, n, 0)
        self.d[slot:slot + 32] = e


def vmu_nova() -> bytes:
    """VMU formatada como o Flycast e o BIOS a deixam: raiz 255, FAT 254, diretorio 253..241."""
    d = bytearray(BLOCOS * BLOCO)
    r = RAIZ * BLOCO
    d[r:r + 16] = b"\x55" * 16
    d[r + 0x10] = 1  # cor propria
    struct.pack_into("<HHHHHH", d, r + 0x46, FAT, 1, DIR, DIR_N, 0, USUARIO)
    fat = [LIVRE] * BLOCOS
    fat[RAIZ] = fat[FAT] = FIM
    for b in range(DIR, DIR - DIR_N, -1):
        fat[b] = b - 1 if b > DIR - DIR_N + 1 else FIM
    struct.pack_into("<256H", d, FAT * BLOCO, *fat)
    return bytes(d)


# ---------------- VMS ----------------

def vms_campos(v: bytes):
    return {
        "desc_curta": v[0x00:0x10].rstrip(b"\0 ").decode("ascii", "replace"),
        "desc_longa": v[0x10:0x30].rstrip(b"\0 ").decode("ascii", "replace"),
        "aplicacao": v[0x30:0x40].hex(),
        "icones": struct.unpack_from("<H", v, 0x40)[0],
        "animacao": struct.unpack_from("<H", v, 0x42)[0],
        "eyecatch": struct.unpack_from("<H", v, 0x44)[0],
        "crc": struct.unpack_from("<H", v, 0x46)[0],
        "tamanho": struct.unpack_from("<I", v, 0x48)[0],
    }


# Campos que o jogo grava (8C01C520 -> 8C08CA5C: strings 8C09BB30/8C09BB3C, payload 0x4B0); o modo
# generico so confere a estrutura, este confere o save do Blue Stinger.
PERFIL_BLUESTINGER = {"desc_curta": "BlueStinger", "desc_longa": "BLUESTINGER SAVE DATA", "icones": 1,
                      "eyecatch": 0, "tamanho": 1200}


def vms_valida(v: bytes, payload=None, tamanho=None, perfil=None):
    """Lista de falhas (vazia = valido) e os campos."""
    f, c = [], vms_campos(v) if len(v) >= CAB else {}
    if len(v) % BLOCO or len(v) < CAB:
        return [f"tamanho {len(v)} nao e multiplo de {BLOCO}"], c
    for k, esperado in (perfil or {}).items():
        if c[k] != esperado:
            f.append(f"{k} = {c[k]!r} (perfil: {esperado!r})")
    if c["eyecatch"] != 0 and not perfil:
        f.append(f"eyecatch {c['eyecatch']}: area de eyecatch nao suportada por este validador")
    if c["icones"] < 1:
        f.append("sem icone")
    if tamanho is not None and c["tamanho"] != tamanho:
        f.append(f"tamanho do payload {c['tamanho']} (esperado {tamanho})")
    usado = CAB + ICONE * max(c["icones"], 1) + c["tamanho"]
    if usado > len(v):
        f.append(f"{usado} bytes usados > {len(v)}")
        return f, c
    if -(-usado // BLOCO) * BLOCO != len(v):
        f.append(f"{len(v) // BLOCO} blocos para {usado} bytes (esperado {-(-usado // BLOCO)})")
    if any(v[usado:]):
        f.append("preenchimento depois do payload nao e zero")
    z = bytearray(v[:usado])
    z[0x46:0x48] = b"\0\0"
    calc = crc16(bytes(z))
    c["crc_calculado"], c["usado"] = calc, usado
    if calc != c["crc"]:
        f.append(f"CRC {c['crc']:04X} gravado, {calc:04X} calculado")
    if payload is not None and v[usado - c["tamanho"]:usado] != payload:
        f.append("payload diferente do esperado")
    return f, c


def vms_sintetico(semente=1, tamanho=1200) -> bytes:
    """VMS com o layout do jogo e conteudo sintetico (nenhum byte do jogo)."""
    rnd = random.Random(semente)
    v = bytearray(CAB)
    v[0x00:0x10] = b"SINTETICO".ljust(16, b" ")
    v[0x10:0x30] = b"N4 ORACULO TESTE".ljust(32, b" ")
    v[0x30:0x40] = bytes(rnd.randrange(256) for _ in range(16))
    struct.pack_into("<HHHHI", v, 0x40, 1, 0, 0, 0, tamanho)
    v[0x60:0x80] = bytes(rnd.randrange(256) for _ in range(32))
    v += bytes(rnd.randrange(256) for _ in range(ICONE))
    v += bytes(rnd.randrange(256) for _ in range(tamanho))
    struct.pack_into("<H", v, 0x46, crc16(bytes(v)))
    v += b"\0" * (-len(v) % BLOCO)
    return bytes(v)


# ---------------- comandos ----------------

def cmd_info(a):
    m = Vmu(open(a.vmu, "rb").read())
    r = m.raiz()
    print(f"formatada: {r['formatada']}; FAT {r['fat']}, diretorio {r['dir']} ({r['dir_n']} blocos); "
          f"{m.livres()} de {r['usuario']} blocos livres")
    for i, _, e in m.entradas():
        print(f"  [{i:3d}] {e['nome']:12s} {e['tipo']:5s} bloco {e['primeiro']:3d}, {e['blocos']} blocos")


def cmd_extrai(a):
    m = Vmu(open(a.vmu, "rb").read())
    os.makedirs(a.pasta, exist_ok=True)
    for _, _, e in m.entradas():
        b = m.le(e["nome"])
        open(os.path.join(a.pasta, e["nome"]), "wb").write(b)
        print(f"{e['nome']}: {len(b)} bytes, sha256 {hashlib.sha256(b).hexdigest()}")


def cmd_valida(a):
    v = open(a.vms, "rb").read()
    p = open(a.payload, "rb").read() if a.payload else None
    falhas, c = vms_valida(v, p, a.tamanho, PERFIL_BLUESTINGER if a.bluestinger else None)
    c.update({"arquivo": os.path.basename(a.vms), "bytes": len(v), "sha256": hashlib.sha256(v).hexdigest(),
              "falhas": falhas})
    if a.json:
        json.dump(c, open(a.json, "w", encoding="utf-8"), indent=1)
    for k, x in c.items():
        if k != "falhas":
            print(f"  {k}: {x:04X}" if k.startswith("crc") else f"  {k}: {x}")
    print("VALIDO" if not falhas else "INVALIDO: " + "; ".join(falhas))
    return 0 if not falhas else 1


def cmd_nova(a):
    open(a.saida, "wb").write(vmu_nova())
    print(f"VMU formatada e vazia -> {a.saida}")


def cmd_prepara(a):
    m = Vmu(open(a.entrada, "rb").read())
    if a.zerada:
        m = Vmu(bytes(BLOCOS * BLOCO))
    elif a.livres is not None:
        k = 0
        while m.livres() > a.livres:  # arquivos de 1 bloco com nomes fora da familia do jogo
            m.grava(f"N4OCUPA.{k:03d}", bytes([k & 0xFF]) * BLOCO)
            k += 1
    elif a.adultera_crc:
        r, o = None, None
        for _, _, e in m.entradas():
            if e["nome"] == a.adultera_crc:
                r = e
        if r is None:
            raise SystemExit(f"{a.adultera_crc} nao esta na VMU")
        o = r["primeiro"] * BLOCO + 0x46  # cabecalho no primeiro bloco do arquivo de dados
        m.d[o] ^= 0xFF
    open(a.saida, "wb").write(bytes(m.d))
    print(f"-> {a.saida}")


def cmd_sintetico(a):
    open(a.saida, "wb").write(vms_sintetico(a.semente))
    print(f"VMS sintetico -> {a.saida}")


def cmd_teste(_a):
    ok = True
    def check(nome, cond):
        nonlocal ok
        ok &= bool(cond)
        print(f"[{'ok' if cond else 'FALHA'}] {nome}")
    check("CRC-16/XMODEM de '123456789' = 31C3", crc16(b"123456789") == 0x31C3)
    v = vms_sintetico(7)
    falhas, c = vms_valida(v, tamanho=1200)
    check("VMS sintetico: 2048 bytes, 4 blocos, valido", len(v) == 2048 and not falhas and c["usado"] == 1840)
    check("payload em 0x280 e zeros de 0x730 a 0x800", not any(v[0x730:]) and c["usado"] - 1200 == 0x280)
    w = bytearray(v); w[0x300] ^= 1
    check("negativo: um bit do payload muda o CRC", vms_valida(bytes(w))[0])
    w = bytearray(v); w[0x7FF] = 1
    check("negativo: preenchimento nao zero", vms_valida(bytes(w))[0])
    check("negativo: payload esperado diferente", vms_valida(v, payload=b"\0" * 1200)[0])
    check("negativo: arquivo truncado", vms_valida(v[:1536])[0])
    check("perfil Blue Stinger: o sintetico (descricoes proprias) e recusado pelos campos",
          any("desc_curta" in x for x in vms_valida(v, perfil=PERFIL_BLUESTINGER)[0]))
    w = bytearray(v); w[0:16] = b"BlueStinger".ljust(16, b" "); w[0x10:0x30] = b"BLUESTINGER SAVE DATA".ljust(32, b" ")
    struct.pack_into("<H", w, 0x46, 0); struct.pack_into("<H", w, 0x46, crc16(bytes(w[:1840])))
    check("perfil Blue Stinger: campos do jogo com CRC refeito passam", not vms_valida(bytes(w), perfil=PERFIL_BLUESTINGER)[0])
    m = Vmu(vmu_nova())
    r = m.raiz()
    check("VMU nova: formatada, 200 livres, sem arquivos", r["formatada"] and m.livres() == 200 and not m.entradas())
    for i in range(6):
        m.grava(f"BLUESTIN.{i:03d}", vms_sintetico(i))
    check("seis slots: 24 blocos ocupados", m.livres() == 176 and len(m.entradas()) == 6)
    check("seis slots: ida e volta byte a byte", all(m.le(f"BLUESTIN.{i:03d}") == vms_sintetico(i) for i in range(6)))
    check("LOAD a partir do bloco 1: payload em +0x80 dos 1536 bytes",
          m.le("BLUESTIN.002")[BLOCO:][0x80:0x80 + 1200] == vms_sintetico(2)[0x280:0x730])
    cheia = Vmu(bytes(m.d))
    k = 0
    while cheia.livres() > 3:
        cheia.grava(f"N4OCUPA.{k:03d}", b"\1" * BLOCO); k += 1
    try:
        cheia.grava("BLUESTIN.009", vms_sintetico(9)); falhou = False
    except ValueError:
        falhou = True
    check("VMU com 3 blocos livres recusa um save de 4", falhou)
    print("autoteste: " + ("ok" if ok else "FALHOU"))
    return 0 if ok else 1


def main():
    p = argparse.ArgumentParser()
    sp = p.add_subparsers(dest="cmd", required=True)
    x = sp.add_parser("info"); x.add_argument("vmu")
    x = sp.add_parser("extrai"); x.add_argument("vmu"); x.add_argument("pasta")
    x = sp.add_parser("valida"); x.add_argument("vms"); x.add_argument("--payload"); x.add_argument("--tamanho", type=int)
    x.add_argument("--json")
    x.add_argument("--bluestinger", action="store_true", help="exige os campos do save do jogo (um icone, sem eyecatch, 1200)")
    x = sp.add_parser("nova"); x.add_argument("saida")
    x = sp.add_parser("prepara"); x.add_argument("entrada"); x.add_argument("saida")
    g = x.add_mutually_exclusive_group(required=True)
    g.add_argument("--livres", type=int); g.add_argument("--zerada", action="store_true"); g.add_argument("--adultera-crc")
    x = sp.add_parser("sintetico"); x.add_argument("saida"); x.add_argument("--semente", type=int, default=1)
    sp.add_parser("teste")
    a = p.parse_args()
    return {"info": cmd_info, "extrai": cmd_extrai, "valida": cmd_valida, "nova": cmd_nova, "prepara": cmd_prepara,
            "sintetico": cmd_sintetico, "teste": cmd_teste}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
