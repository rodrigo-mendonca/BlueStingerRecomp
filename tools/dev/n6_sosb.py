"""N6 fase C: catalogo dos efeitos do BS_SND.MLT por (banco, pedido), o par que o jogo passa ao SDK
(8C15E088(h, id/128, id%128, 0); docs/N6-PLANO.md secoes 10 e 13). So leitura, nada do jogo e copiado.

Formato (conferido contra o oraculo O6, que leu os registradores da AICA no key-on dos IDs 117 e 90):
  SMLT: tabela de unidades (tipo, banco, endereco na RAM de som, tamanho, deslocamento no arquivo)
  SOSB (uma por banco de one-shots): "SOSB", versao, tamanho, n (pedidos), n deslocamentos (u32) dos descritores
  SOSP (descritor, 0x38 bytes):
    +4  byte: SA bits 23-16      +5 byte: indicadores (1 ou 3; hipotese: bit 0 ADPCM, bit 1 laco - PENDENTE)
    +6  u16: SA bits 15-0 (SA relativo ao inicio da unidade SOSB na RAM de som)
    +8  u16: LSA   +10 u16: LEA (amostras)          +16 u32: OCT (bits 14-11, com sinal) e FNS (bits 9-0)
  Medido: ID 117 -> SA 0x0B92DC (+ 0x097A60 = 0x150D3C), LEA 0xE25B, OCT -2, FNS 833;
          ID 90  -> SA 0x087FC4 (0x11FA24), LEA 0x3080, OCT -2, FNS 0.
  Taxa (AICA): 44100 * 2^OCT * (1 + FNS/1024) Hz.

    python tools/dev/n6_sosb.py <BS_SND.MLT> [--tsv <saida>]
"""
import argparse
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import n6_inventario as inv  # noqa: E402


def entries(path):
    b = open(path, "rb").read()
    out = []
    for u in inv.smlt(path):
        if u["tipo"] != "SOSB":
            continue
        d0 = b[u["arquivo"]:u["arquivo"] + u["tamanho_arquivo"]]
        if d0[:4] != b"SOSB":
            raise SystemExit(f"unidade SOSB banco {u['banco']}: assinatura errada")
        n = struct.unpack_from("<I", d0, 12)[0]
        tab = struct.unpack_from(f"<{n}I", d0, 0x10)
        for req, o in enumerate(tab):
            d = d0[o:o + 0x38]
            if d[:4] != b"SOSP":
                raise SystemExit(f"banco {u['banco']} pedido {req}: descritor sem SOSP")
            sa = (d[4] << 16) | struct.unpack_from("<H", d, 6)[0]
            lsa, lea = struct.unpack_from("<HH", d, 8)
            pitch = struct.unpack_from("<I", d, 16)[0]
            oct_ = (pitch >> 11) & 0xF
            oct_ = oct_ - 16 if oct_ >= 8 else oct_
            fns = pitch & 0x3FF
            out.append(dict(banco=u["banco"], pedido=req, id=u["banco"] * 128 + req, sa=sa, sa_ram=u["aica"] + sa,
                            indicadores=d[5], lsa=lsa, lea=lea, oct=oct_, fns=fns,
                            taxa=round(44100 * 2.0 ** oct_ * (1 + fns / 1024.0), 2),
                            arquivo=u["arquivo"] + sa))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mlt")
    p.add_argument("--tsv")
    a = p.parse_args()
    es = entries(a.mlt)
    # conferencia contra o medido no oraculo O6 (nao muda: e o mesmo disco)
    medido = {117: (0x150D3C, 0xE25B, -2, 833), 90: (0x11FA24, 0x3080, -2, 0)}
    for e in es:
        if e["id"] in medido:
            ok = (e["sa_ram"], e["lea"], e["oct"], e["fns"]) == medido[e["id"]]
            print(f"ID {e['id']}: {'confere com o O6' if ok else 'DIVERGE do O6'}")
            if not ok:
                return 1
    print(f"{len(es)} efeitos; com indicador 3: {sum(e['indicadores'] == 3 for e in es)}; com LSA != 0: {sum(e['lsa'] != 0 for e in es)}")
    if a.tsv:
        with open(a.tsv, "w", encoding="utf-8") as f:
            cols = list(es[0].keys())
            f.write("\t".join(cols) + "\n")
            for e in es:
                f.write("\t".join(str(e[c]) for c in cols) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
