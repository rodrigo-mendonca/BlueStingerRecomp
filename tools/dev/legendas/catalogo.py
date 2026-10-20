"""Legendas EN (docs/LEGENDAS-EN-PLANO.md), passo 1: o catalogo das falas a partir do binario e do disco do USUARIO.

Le a tabela de nomes do player de falas (0x8C157754, a mesma do tools/dev/n6_vozes.py) no 1ST_READ.BIN, confere cada
nome com os WAV de BSDATA, decodifica o ADPCM da AICA (copia do native/audio/aica_adpcm.cpp, conferida pelos testes de
tests/dev/test_legendas.py) em copias PCM de 16 bits para a analise, e grava o catalogo. Nada daqui vai para o Git: a
saida fica na pasta de trabalho (padrao %LOCALAPPDATA%\\BlueStingerRecomp\\legendas-en).

    python tools/dev/legendas/catalogo.py <1ST_READ.BIN> <BSDATA> [--saida <pasta>] [--sem-pcm]

Categoria (com a fonte e a confianca, sem inventar relacao com cenas):
  ambiente  o nome termina em ENV (convencao dos arquivos; confianca "nome") - a confirmar pela transcricao
  fala?     os demais: candidatos a fala, ate a deteccao de voz (passo 2) dizer "fala" ou "sem-fala"
  vazio     ID sem nome na tabela (o player ignora o pedido)
Contexto: so o comprovado - chamadores estaticos (tabelas dos dois chamadores do player) e o que os oraculos e as
rotas do N6 tocaram (com a rota). O resto fica "desconhecido".
"""
import argparse
import hashlib
import os
import struct
import sys
import wave

BASE, TABELA, LIMITE = 0x8C010000, 0x8C157754, 512
# chamadores do player 8C08B860 (docs/N6-PLANO.md secao 16 e docs/LOCALIZACAO-PLANO.md secao 6)
TABELA_8C02E0C0 = 0x8C0AAD20  # 16 entradas {u16 id, u16 x}; o indice e sorteado pelo chamador
# vistos tocando (rota passo_combate: NEW GAME ate o combate; oraculo Flycast falas_tmu*.tsv e n6_falas.ps1)
VISTOS = {
    221: "rota passo_combate: no carregamento do NEW GAME (Flycast e nativo)",
    222: "rota passo_combate: inicio do gameplay, area 1 (Flycast e nativo)",
    4: "rota passo_combate: depois do fim do E012BENV, inicio do gameplay (Flycast e nativo)",
    5: "rota passo_combate: depois do E002A (nativo, 12 000 VBlanks)",
    204: "rota passo_combate: gameplay (nativo, 2 vezes)",
}

KDIFF = (1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15)
KSTEP = (230, 230, 230, 230, 307, 409, 512, 614, 230, 230, 230, 230, 307, 409, 512, 614)


def _trunc(a, b):  # divisao inteira do C++ (trunca para zero)
    q = abs(a) // b
    return q if a >= 0 else -q


def adpcm_aica(data, amostras=None):
    """O decodificador do native/audio/aica_adpcm.cpp (nibble baixo primeiro; sinal 0, passo 127 no inicio)."""
    n = len(data) * 2 if amostras is None else min(amostras, len(data) * 2)
    sig, stp, out = 0, 127, [0] * n
    for i in range(n):
        nib = (data[i >> 1] >> 4) if i & 1 else (data[i >> 1] & 15)
        sig += _trunc(stp * KDIFF[nib], 8)
        sig = -32768 if sig < -32768 else 32767 if sig > 32767 else sig
        stp = _trunc(stp * KSTEP[nib], 256)
        stp = 127 if stp < 127 else 24576 if stp > 24576 else stp
        out[i] = sig
    return out


def nomes(binario):
    b = open(binario, "rb").read()
    r = []
    for i in range(LIMITE):
        p = struct.unpack_from("<I", b, TABELA - BASE + 4 * i)[0]
        if not BASE <= p < BASE + len(b):
            break
        r.append(b[p - BASE:p - BASE + 16].split(b"\0")[0].decode("latin1"))
    chamador = {}
    for k in range(16):
        vid = struct.unpack_from("<H", b, TABELA_8C02E0C0 - BASE + 4 * k)[0]
        chamador.setdefault(vid, []).append(k)
    return r, chamador


def wav_info(raw):
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        return None
    i, fmt, data = 12, None, None
    while i + 8 <= len(raw):
        cid, sz = raw[i:i + 4], struct.unpack_from("<I", raw, i + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", raw, i + 8)
        elif cid == b"data":
            data = raw[i + 8:i + 8 + sz]
        i += 8 + sz + (sz & 1)
    return (fmt, data) if fmt and data is not None else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binario")
    ap.add_argument("bsdata")
    ap.add_argument("--saida", default=os.path.join(os.environ.get("LOCALAPPDATA", "."), "BlueStingerRecomp", "legendas-en"))
    ap.add_argument("--sem-pcm", action="store_true", help="so o catalogo (sem as copias PCM)")
    a = ap.parse_args()
    pcm_dir = os.path.join(a.saida, "pcm")
    os.makedirs(pcm_dir, exist_ok=True)
    ns, chamador = nomes(a.binario)
    arquivos = {f.upper(): f for f in os.listdir(a.bsdata) if f.upper().endswith(".WAV")}
    linhas, problemas = [], []
    for vid, nome in enumerate(ns):
        reg = dict(id=vid, nome=nome or "-", existe="", canais="", taxa="", bits="", amostras="", segundos="", sha256="",
                   categoria="vazio" if not nome else "", fonte_categoria="tabela" if not nome else "",
                   contexto="", fonte_contexto="", pcm="", transcricao="sem-fala" if not nome else "pendente")
        ctx, fonte = [], []
        if vid in chamador:
            ctx.append(f"8C02E0C0 (tabela 0x8C0AAD20, indices {chamador[vid]}, sorteio)")
            fonte.append("estatico")
        if vid in VISTOS:
            ctx.append(VISTOS[vid])
            fonte.append("oraculo/rota")
        reg["contexto"] = "; ".join(ctx) or "desconhecido"
        reg["fonte_contexto"] = ",".join(fonte) or "-"
        if nome:
            f = arquivos.get(nome.upper())
            reg["existe"] = "sim" if f else "nao"
            if not f:
                problemas.append(f"id {vid}: {nome} sem arquivo")
            else:
                raw = open(os.path.join(a.bsdata, f), "rb").read()
                reg["sha256"] = hashlib.sha256(raw).hexdigest()
                w = wav_info(raw)
                if not w:
                    problemas.append(f"id {vid}: {nome} nao e RIFF/WAVE")
                else:
                    (tag, ch, rate, _, _, bits), data = w
                    am = len(data) * 8 // (bits * ch) if bits else 0
                    reg.update(canais=ch, taxa=rate, bits=bits, amostras=am, segundos=f"{am / rate:.3f}")
                    if nome.upper().endswith("ENV.WAV"):
                        reg["categoria"], reg["fonte_categoria"] = "ambiente", "nome (ENV)"
                    else:
                        reg["categoria"], reg["fonte_categoria"] = "fala?", "nome (a confirmar pela deteccao de voz)"
                    destino = os.path.join(pcm_dir, os.path.splitext(f)[0] + ".wav")
                    if tag == 0 and bits == 4 and ch == 1:
                        if not a.sem_pcm and not os.path.exists(destino):
                            pcm = adpcm_aica(data, am)
                            with wave.open(destino, "wb") as o:
                                o.setnchannels(1), o.setsampwidth(2), o.setframerate(rate)
                                o.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
                        reg["pcm"] = os.path.basename(destino) if os.path.exists(destino) else ""
                    else:
                        # ADPCM estereo: a disposicao dos canais no arquivo ainda nao foi medida (o player nativo do N6
                        # tambem recusa: pendente); nada de adivinhar
                        reg["pcm"] = ""
                        reg["transcricao"] = "bloqueada (ADPCM estereo: disposicao dos canais pendente)"
                        problemas.append(f"id {vid}: {nome} ADPCM com {ch} canais: disposicao pendente")
        linhas.append(reg)
    sobra = sorted(set(arquivos) - {n.upper() for n in ns if n})
    for s in sobra:
        problemas.append(f"WAV sem ID: {s}")
    cols = list(linhas[0].keys())
    with open(os.path.join(a.saida, "catalogo.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("\t".join(cols) + "\n")
        for r in linhas:
            o.write("\t".join(str(r[c]) for c in cols) + "\n")
    n_nome = sum(1 for r in linhas if r["nome"] != "-")
    print(f"{len(linhas)} IDs, {n_nome} com nome, {len(arquivos)} WAV no disco; "
          f"{sum(1 for r in linhas if r['categoria'] == 'ambiente')} ambientes (nome), "
          f"{sum(1 for r in linhas if r['categoria'] == 'fala?')} candidatos a fala, "
          f"{sum(1 for r in linhas if r['pcm'])} copias PCM -> {a.saida}")
    for p in problemas:
        print("  " + p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
