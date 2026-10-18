"""Roteiros de entrada do N3-0 (docs/N3-0-RELATORIO.md), gerados de forma reproduzivel.

  python tools/dev/pad_roteiros.py <pasta de saida>

Gera (todos "indice passo"):
  pad_matriz.txt     cobertura do contrato do servidor pd: os 256 valores de cada byte (gatilho R,
                     gatilho L, eixo X, eixo Y), cada botao sozinho, todos os pares de botoes,
                     gatilhos com botoes, todos juntos; um estado por passo desde o boot
  pad_matriz2.txt    os 256 valores de x2 e y2 (com BS_MAPLE_FUNCAO=FE063F00, support com os bits 20-21) e
                     trocas de botao sem soltura no meio (old e on diferentes, os dois nao nulos)
  pad_suporte.txt    curto, para as execucoes com BS_MAPLE_FUNCAO (64 combinacoes de support)
  passo_limiares.txt passo_cgate ate a sala RCG02 e depois os pontos de fronteira do jogo
                     (zona morta 20, direcional 48, gatilho 127/128), cada um por 8 passos
  passo_conexao.txt  passo_cgate e depois: desconectar com A apertado, reconectar neutro,
                     desconectar e reconectar com A apertado
  pad_softreset.txt  A+B+X+Y e depois START (soft reset)
  pad_conexao_boot.txt  porta vazia desde o boot, conecta no passo 60 e no titulo
  pad_transicao.txt  "indice transicao": troca de estado a cada transicao injetada (no primeiro acesso ao MDST
                     depois do VBlank-In, antes do disparo do DMA): negativo do ponto de amostragem

Os pontos de fronteira e os passos de cada caso vao tambem para <pasta>/limiares.tsv e
<pasta>/conexao.tsv (o teste confere o traco do jogo contra eles).
"""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUTTONS = ["C", "B", "A", "START", "UP", "DOWN", "LEFT", "RIGHT", "Z", "Y", "X", "D"]
# o controle padrao so tem estes no support 000F06FE (bits 1-7, 9, 10)
STD = ["B", "A", "START", "UP", "DOWN", "LEFT", "RIGHT", "Y", "X"]


def cgate_lines():
    out = []
    with open(os.path.join(REPO, "tools", "dist", "roteiros", "passo_cgate.txt"), encoding="utf-8") as f:
        for line in f:
            t = line.split("#")[0].strip()
            if t and not t.startswith("indice"):
                out.append(t)
    last = int(out[-1].split()[0])
    return out, last


def write(path, header, lines):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.write("indice passo\n" if not header.endswith("transicao\n") else "")
        for l in lines:
            f.write(l + "\n")


def matrix():
    states = []
    for key in ("RT", "LT", "AX", "AY"):
        states += [f"{key}={v}" for v in range(256)]
    states += BUTTONS
    for i, a in enumerate(BUTTONS):
        for b in BUTTONS[i + 1:]:
            states.append(f"{a}+{b}")
    states += ["A+RT", "B+LT", "X+RT=128", "Y+LT=128", "UP+RT+LT", "A+B+RT=127+LT=127", "RT+LT"]
    # todos juntos menos START: A+B+X+Y+START e o soft reset (caso proprio, pad_softreset.txt)
    states.append("+".join(b for b in BUTTONS if b != "START"))
    lines, step = [], 30  # o servidor ja roda desde os primeiros passos
    for s in states:
        lines.append(f"{step} {s}")
        step += 1
        lines.append(f"{step} -")  # um passo solto entre estados: bordas de soltura tambem
        step += 1
    return lines


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "pad_matriz.txt"), "# gerado por tools/dev/pad_roteiros.py\n", matrix())
    m2, step = [], 30
    for s in [f"A2X={v}" for v in range(256)] + [f"A2Y={v}" for v in range(256)] + ["A", "A+B", "B", "X+B", "X", "Y+X", "-"]:
        m2.append(f"{step} {s}")
        step += 1
    write(os.path.join(out, "pad_matriz2.txt"), "# gerado por tools/dev/pad_roteiros.py\n", m2)
    sup = ["30 RT=200+LT=100+AX=10+AY=250", "32 -", "34 A+RT", "36 -", "38 AX=0+AY=0", "40 AX=255+AY=255", "42 RT=127+LT=127", "44 RT=128+LT=128", "46 -"]
    write(os.path.join(out, "pad_suporte.txt"), "# gerado por tools/dev/pad_roteiros.py\n", sup)

    base, last = cgate_lines()
    start = last + 30
    # limiares: valor do byte -> (x1/y1 depois da zona morta, direcao esperada na palavra de acoes)
    vals = [79, 80, 81, 107, 108, 109, 128, 147, 148, 149, 175, 176, 177]
    lim, rows, step = [], [], start
    for key in ("AX", "AY"):
        for b in vals:
            lim.append(f"{step} {key}={b}")
            rows.append(f"{key}\t{b}\t{step}")
            step += 8
    for key in ("RT", "LT"):
        for b in (127, 128):
            lim.append(f"{step} {key}={b}")
            rows.append(f"{key}\t{b}\t{step}")
            step += 8
    lim.append(f"{step} -")
    write(os.path.join(out, "passo_limiares.txt"), "# gerado por tools/dev/pad_roteiros.py (passo_cgate + limiares)\n",
          base + lim)
    with open(os.path.join(out, "limiares.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("campo\tbyte\tpasso\n" + "\n".join(rows) + "\n")

    s = start
    con = [f"{s} A", f"{s + 20} DESCONECTADO", f"{s + 60} -", f"{s + 100} DESCONECTADO", f"{s + 140} A", f"{s + 180} -"]
    write(os.path.join(out, "passo_conexao.txt"), "# gerado por tools/dev/pad_roteiros.py (passo_cgate + conexao)\n",
          base + con)
    with open(os.path.join(out, "conexao.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("caso\tpasso\n")
        f.write(f"segurando_A\t{s}\ndesconecta_com_A\t{s + 20}\nreconecta_neutro\t{s + 60}\n"
                f"desconecta\t{s + 100}\nreconecta_com_A\t{s + 140}\nsolta\t{s + 180}\n")
    write(os.path.join(out, "pad_conexao_boot.txt"), "# gerado por tools/dev/pad_roteiros.py\n",
          ["0 DESCONECTADO", "60 -", "101 START", "116 -"])
    # soft reset (8C064878): A+B+X+Y segurados e START apertado; o jogo pede o menu do BIOS
    write(os.path.join(out, "pad_softreset.txt"), "# gerado por tools/dev/pad_roteiros.py\n",
          ["30 A+B+X+Y", "40 A+B+X+Y+START", "50 -"])
    write(os.path.join(out, "pad_softreset_controle.txt"), "# gerado por tools/dev/pad_roteiros.py\n",
          ["30 A+B+X+Y", "60 -"])
    # indice transicao: o estado alterna a cada transicao injetada (A, -, A, -...), a partir da 300a
    vb = [f"{v} {'A' if v % 2 == 0 else '-'}" for v in range(300, 2300)]
    write(os.path.join(out, "pad_transicao.txt"), "# gerado por tools/dev/pad_roteiros.py\nindice transicao\n", vb)
    print("ok", out)


if __name__ == "__main__":
    main()
