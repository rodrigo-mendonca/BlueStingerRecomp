"""Converte um roteiro indexado por leitura do controle em "indice passo" (N3-0).

  python tools/dev/converte_roteiro.py <leituras.tsv> <saida.txt> [--origem <roteiro original>]

<leituras.tsv> e o BS_PAD_LEITURAS de uma execucao com o roteiro original (leitura, passo, VBlank,
estado servido). No modo nativo o provedor nao tem leituras de GET_CONDITION, so passos; o roteiro
por passo vale igual nos modos de tempo e nos provedores (legado e nativo).

Regra: o estado de cada passo e o da ULTIMA leitura feita durante o passo. A camada per do SDK
recolhe no VBlank a resposta do DMA anterior, e o servidor do passo seguinte usa a mais recente; com
todas as leituras do passo iguais, a amostra consumida e a mesma qualquer que seja. A equivalencia e
provada pelo teste (tools/dist/test_controles.ps1, K12): traco de entrada e quadros iguais ao original.
Leituras feitas antes do primeiro passo (passo 0) valem desde o passo 0.
"""
import sys


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    src, out = a[0], a[1]
    origin = a[a.index("--origem") + 1] if "--origem" in a else src
    per_step = {}
    with open(src, encoding="utf-8") as f:
        f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < 4:
                continue
            step = int(c[1])
            if step < 0:
                continue
            per_step[step] = c[3]  # a ultima leitura do passo prevalece
    lines, prev = [], None
    for step in sorted(per_step):
        st = per_step[step]
        if st != prev:
            lines.append(f"{step} {st}")
            prev = st
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# Convertido de {origin} por tools/dev/converte_roteiro.py (estado da ultima leitura de cada passo)\n")
        f.write("indice passo\n")
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines)} linhas -> {out}")


if __name__ == "__main__":
    main()
