"""N6 fase F: perda de ticks do driver Manatee, medida direto (docs/N6-PLANO.md 18.6).

Entradas: o oraculo com --ticks (cada passo: t = contador de ticks reais do driver em ARM 0x13418, incrementado pelo
tratador de 1 kHz a cada 4 interrupcoes; p = ponteiro de leitura da trilha 0 menos a base do SMSD; c = contagem da trilha,
[fp+0x24]) e os eventos do bs-musica da mesma musica (linhas "pos": depois de cada chamada nativa, o mesmo p e o mesmo c).

O sequenciador nativo faz a mesma aritmetica inteira do driver, entao o par (p, c) de uma leitura do oraculo identifica
a chamada nativa exata (a contagem cai um passo por chamada; dois instantes com o mesmo par so existem em lacos e
referencias, separados por muitas chamadas). O pareamento anda so para a frente, na primeira ocorrencia do par a partir
da anterior, e recusa saltos maiores que a margem dada. Para cada leitura: atraso = (t - t0) - (chamada - c0), em ticks;
sem perda o atraso fica constante; um tick perdido pelo driver o aumenta em 1.

    python tools/dev/n6_ticks.py <oraculo.tsv> <eventos.tsv> [--ate-passo N] [--margem 500]
"""
import argparse
import collections
import re


def oraculo(caminho, ate):
    amostras, tocar = [], False
    for linha in open(caminho, encoding="utf-8", errors="replace"):
        c = linha.rstrip("\n").split("\t")
        if len(c) < 4 or not c[0].isdigit():
            continue
        if ate is not None and int(c[0]) >= ate:
            break
        if c[2] == "mus-tocar":
            tocar = True
        if c[2] == "ticks" and tocar:
            m = re.search(r"t=(\d+) p=(\d+) c=(-?\d+)", c[3])
            if m:
                amostras.append((int(c[0]), int(m.group(1)), int(m.group(2)), int(m.group(3))))
    return amostras


def nativo(caminho):
    por = collections.defaultdict(list)  # (p, c) -> chamadas, em ordem
    for linha in open(caminho, encoding="utf-8"):
        c = linha.rstrip("\n").split("\t")
        if len(c) >= 8 and c[2] == "pos":
            m = re.match(r"p=(\d+) c=(-?\d+)", c[7])
            if m:
                por[(int(m.group(1)), int(m.group(2)))].append(int(c[0]))
    return por


def main():
    a = argparse.ArgumentParser()
    a.add_argument("oraculo")
    a.add_argument("eventos")
    a.add_argument("--ate-passo", type=int, default=None)
    a.add_argument("--margem", type=int, default=500, help="maior salto aceito entre chamadas de leituras seguidas")
    a = a.parse_args()
    amostras, por = oraculo(a.oraculo, a.ate_passo), nativo(a.eventos)
    if not amostras or not por:
        raise SystemExit(f"sem dados: amostras {len(amostras)}, pares nativos {len(por)}")
    pares, sem, ultima = [], 0, None
    for passo, t, p, c in amostras:
        chamadas = por.get((p, c), [])
        cand = [k for k in chamadas if ultima is None or (ultima <= k <= ultima + a.margem)]
        if not cand:
            sem += 1
            continue
        ultima = cand[0]
        pares.append((passo, t, ultima))
    if not pares:
        raise SystemExit("nenhuma leitura do oraculo achou o mesmo (p, c) no nativo")
    t0, c0 = pares[0][1], pares[0][2]
    atrasos = [(passo, (t - t0) - (k - c0)) for passo, t, k in pares]
    print(f"leituras do oraculo depois do tocar: {len(amostras)}; com o mesmo (ponteiro, contagem) no nativo: {len(pares)} "
          f"(sem par: {sem})")
    print("atraso do driver = ticks reais - chamadas nativas desde a 1a leitura pareada (ticks de 4 ms)")
    blocos = collections.OrderedDict()
    for passo, d in atrasos:
        blocos.setdefault(passo // 100 * 100, []).append(d)
    for b, ds in blocos.items():
        print(f"  passos {b}-{b + 99}: {len(ds):4d} leituras, atraso min {min(ds):+d} max {max(ds):+d}")
    ds = [d for _, d in atrasos]
    print(f"atraso: inicio {ds[0]:+d}, fim {ds[-1]:+d}; variacao {ds[-1] - ds[0]:+d} ticks ({(ds[-1] - ds[0]) * 4:+d} ms)")


if __name__ == "__main__":
    main()
