"""N6 fase F (docs/N6-PLANO.md secao 18): os bancos de programas e amostras do Manatee (SMPB, M*.MPB), lidos pela
estrutura que os dados mostram e conferidos em todos os bancos do disco. Nada daqui sai do disco do usuario.

Cabecalho: 'SMPB', versao, tamanho, 0; depois 4 pares (deslocamento, contagem):
  programas (deslocamento de cada programa), curvas de velocidade (128 bytes cada) e duas tabelas pequenas.
Programa: 2 ponteiros de grupo (+0, +4) e, em +0x18, os grupos: (contagem de camadas, deslocamento, 0, indicadores).
Camada (0x30 bytes; os campos do descritor SOSP dos efeitos sem a assinatura, mais a faixa de teclas):
  +0 SA (bits 22-16) e +1 indicadores (bit 0 ADPCM, bit 1 laco); +2 SA (bits 15-0) relativo ao banco
  +4 LSA, +6 LEA; +0x13 DISDL; +0x15 TL base; +0x16..+0x1F envelope (5 palavras da AICA)
  +0x24 tecla minima, +0x25 tecla maxima, +0x26 tecla base, +0x27 ajuste fino; +0x2C velocidade maxima (a medir)
Os dois grupos de um programa tocam juntos (o grupo 1 repete as amostras do 0 com outro ajuste fino ou TL); as faixas de
teclas cobrem so o alcance do instrumento. Campos sem prova no driver ficam como "a medir" (o oraculo da fase F confere).

    python tools/dev/n6_smpb.py <BSDATA> [--programas M2A]
"""
import argparse
import os
import struct
import sys

CAMADA = 0x30


class Erro(Exception):
    pass


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def smpb(path):
    b = open(path, "rb").read()
    if b[:4] != b"SMPB":
        raise Erro("nao e SMPB")
    tabelas = [(u32(b, 0x10 + 8 * k), u32(b, 0x14 + 8 * k)) for k in range(4)]
    off_prog, n_prog = tabelas[0]
    progs = []
    for p in range(n_prog):
        po = u32(b, off_prog + 4 * p)
        grupos = []
        for gk, go in enumerate((u32(b, po), u32(b, po + 4))):
            if go == 0:
                continue
            n, lo, _, ind = struct.unpack_from("<IIII", b, go)
            camadas = []
            for k in range(n):
                c = lo + k * CAMADA
                if c + CAMADA > len(b):
                    raise Erro(f"programa {p} grupo {gk}: camada {k} fora do arquivo")
                d = b[c:c + CAMADA]
                sa = ((d[0] & 0x7F) << 16) | struct.unpack_from("<H", d, 2)[0]
                lsa, lea = struct.unpack_from("<HH", d, 4)
                camadas.append(dict(sa=sa, ind=d[1], lsa=lsa, lea=lea, disdl=d[0x13], tl=d[0x15], env=d[0x16:0x20].hex(),
                                    kmin=d[0x24], kmax=d[0x25], base=d[0x26], fino=d[0x27], vmax=d[0x2C], bruto=d.hex()))
            grupos.append(dict(n=n, off=lo, ind=ind, camadas=camadas))
        progs.append(dict(indice=p, off=po, grupos=grupos))
    return b, tabelas, progs


def conferir(b, progs):
    """Problemas de estrutura: amostra ou laco fora do arquivo. As faixas de teclas NAO cobrem 0..127 (um instrumento
    cobre a sua faixa) e o grupo 1 repete o grupo 0 com outro ajuste (camadas simultaneas): so contados, nao sao erro."""
    prob, sobrepostas = [], 0
    for p in progs:
        for gk, g in enumerate(p["grupos"]):
            faixas = sorted((c["kmin"], c["kmax"]) for c in g["camadas"])
            sobrepostas += sum(1 for (a0, a1), (b0, _) in zip(faixas, faixas[1:]) if b0 <= a1)
            for c in g["camadas"]:
                if c["kmin"] > c["kmax"]:
                    prob.append(f"programa {p['indice']} grupo {gk}: faixa de teclas invertida {c['kmin']}-{c['kmax']}")
                fim = c["sa"] + (c["lea"] + 1) // 2 if c["ind"] & 1 else c["sa"] + 2 * c["lea"]
                if fim > len(b):
                    prob.append(f"programa {p['indice']}: amostra {c['sa']:#x}+{c['lea']} fora do arquivo ({len(b):#x})")
                if c["ind"] & 2 and not c["lsa"] < c["lea"]:
                    prob.append(f"programa {p['indice']}: laco LSA {c['lsa']} >= LEA {c['lea']}")
    return prob, sobrepostas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bsdata")
    ap.add_argument("--programas", default="")
    a = ap.parse_args()
    total = 0
    for f in sorted(os.listdir(a.bsdata)):
        if not f.upper().endswith(".MPB"):
            continue
        nome = f[:-4].upper()
        b, tab, progs = smpb(os.path.join(a.bsdata, f))
        prob, sobrepostas = conferir(b, progs)
        n_cam = sum(len(g["camadas"]) for p in progs for g in p["grupos"])
        inds = sorted({c["ind"] for p in progs for g in p["grupos"] for c in g["camadas"]})
        print(f"{nome}: tabelas {[(hex(o), n) for o, n in tab]}; {len(progs)} programas, {n_cam} camadas, indicadores {inds}; "
              f"{sobrepostas} faixas sobrepostas no mesmo grupo; {len(prob)} problemas")
        for x in prob[:6]:
            print("   ", x)
        total += len(prob)
        if nome == a.programas.upper():
            for p in progs:
                for gk, g in enumerate(p["grupos"]):
                    for c in g["camadas"]:
                        print(f"    p{p['indice']} g{gk} ind{g['ind']:#x} teclas {c['kmin']}-{c['kmax']} base {c['base']} fino {c['fino']} "
                              f"sa {c['sa']:#x} ind {c['ind']} lsa {c['lsa']} lea {c['lea']} tl {c['tl']} vmax {c['vmax']}")
    print(f"{total} problemas no total")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
