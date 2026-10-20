"""N6 fase E (docs/N6-PLANO.md secao 16): o mapa das falas do jogo, do binario e do disco do usuario.

O player de voz do jogo, 8C08B860(id), le o nome em *(0x8C157754 + 4*id) e chama o MW WAV da CRI (PlyCreateWav
8C158FC4, tabela de metodos 8C1DD248). Este script le a tabela de nomes do 1ST_READ.BIN, confere cada nome contra
os WAV do disco (data\\disc\\BSDATA) e grava id, nome, canais, taxa, bits, amostras e duracao. Sem bytes do jogo na
saida: so nomes, numeros e o resultado da conferencia.

    python tools/dev/n6_vozes.py <1ST_READ.BIN> <pasta BSDATA> <saida.tsv>
Sai com 1 se um nome nao tiver arquivo, se um WAV nao for usado ou se o formato nao for o esperado.
"""
import os
import struct
import sys

BASE, TABELA, LIMITE = 0x8C010000, 0x8C157754, 512


def nomes(binario):
    b = open(binario, "rb").read()
    r = []
    for i in range(LIMITE):
        p = struct.unpack_from("<I", b, TABELA - BASE + 4 * i)[0]
        if not BASE <= p < BASE + len(b):  # fim da tabela: a palavra seguinte nao e ponteiro para o executavel
            break
        r.append(b[p - BASE:p - BASE + 16].split(b"\0")[0].decode("latin1"))
    return r


def wav(path):
    x = open(path, "rb").read()
    if x[:4] != b"RIFF" or x[8:12] != b"WAVE":
        return None
    i, fmt, data = 12, None, None
    while i + 8 <= len(x):
        cid, sz = x[i:i + 4], struct.unpack_from("<I", x, i + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", x, i + 8)
        elif cid == b"data":
            data = sz
        i += 8 + sz + (sz & 1)
    if not fmt or data is None:
        return None
    tag, ch, rate, _, _, bits = fmt
    amostras = data * 8 // (bits * ch) if bits else 0
    return tag, ch, rate, bits, amostras


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    binario, pasta, saida = sys.argv[1:]
    ns = nomes(binario)
    arquivos = {f.upper(): f for f in os.listdir(pasta) if f.upper().endswith(".WAV")}
    erros, usados = [], set()
    with open(saida, "w", encoding="utf-8", newline="\n") as o:
        o.write("id\tnome\tcanais\ttaxa\tbits\tformato\tamostras\tsegundos\n")
        for i, n in enumerate(ns):
            if not n:
                o.write(f"{i}\t-\t\t\t\t\t\t\n")
                continue
            f = arquivos.get(n.upper())
            if not f:
                erros.append(f"id {i}: {n} sem arquivo")
                continue
            usados.add(n.upper())
            w = wav(os.path.join(pasta, f))
            if not w or w[0] != 0 or w[3] != 4:  # formato 0 com 4 bits: ADPCM da AICA (o decodificador do N6)
                erros.append(f"id {i}: {n} formato inesperado {w}")
                continue
            tag, ch, rate, bits, am = w
            o.write(f"{i}\t{n}\t{ch}\t{rate}\t{bits}\t{tag}\t{am}\t{am / rate:.3f}\n")
    sobra = sorted(set(arquivos) - usados)
    if sobra:
        erros.append(f"WAV sem id: {sobra}")
    vazios = [i for i, n in enumerate(ns) if not n]
    print(f"{len(ns)} ids (0-{len(ns) - 1}), {len(ns) - len(vazios)} com nome, vazios {vazios}; "
          f"{len(arquivos)} WAV no disco; {len(erros)} problemas -> {saida}")
    for e in erros:
        print("  " + e)
    return 1 if erros else 0


if __name__ == "__main__":
    sys.exit(main())
