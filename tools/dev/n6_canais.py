"""N6 fase G: como o driver Manatee reaproveita os 48 canais da musica (alocador 0x1244), visto nas linhas "aica" do
oraculo (n6_oraculo.py): para cada key-on num canal de musica (0-47), o canal estava livre (key-off ja dado: soltando ou
parado) ou ainda com KYONB (roubo de uma nota tocando)? E quantos canais tinham KYONB naquela hora (ocupacao).

A leitura e por passo: um canal que soltou e voltou a tocar dentro do mesmo passo aparece como k1 -> k1 com outra amostra
(contado a parte como "troca de amostra sem key-off visto").

    python tools/dev/n6_canais.py <oraculo.tsv> [--de-passo N] [--ate-passo N]
"""
import argparse
import collections
import re

RE_CANAL = re.compile(r"c(\d+):k([01]) tl\d+ sdl\d+ pan\d+(?: \[sa=([0-9A-F]+))?")


def main():
    a = argparse.ArgumentParser()
    a.add_argument("oraculo")
    a.add_argument("--de-passo", type=int, default=0)
    a.add_argument("--ate-passo", type=int, default=1 << 30)
    a = a.parse_args()
    k = {}       # canal -> KYONB atual
    sa = {}      # canal -> amostra atual
    eventos = collections.Counter()
    ocupacao = collections.Counter()
    for linha in open(a.oraculo, encoding="utf-8", errors="replace"):
        c = linha.rstrip("\n").split("\t")
        if len(c) < 4 or c[2] != "aica" or not c[0].isdigit():
            continue
        passo = int(c[0])
        for m in RE_CANAL.finditer(c[3]):
            ch, kk, s = int(m.group(1)), int(m.group(2)), m.group(3)
            if ch >= 48:
                continue
            if a.de_passo <= passo <= a.ate_passo and kk == 1 and s:
                antes = k.get(ch, 0)
                if antes == 0:
                    eventos["key-on em canal livre (sem KYONB)"] += 1
                elif sa.get(ch) != s:
                    eventos["key-on em canal com KYONB e outra amostra (roubo ou re-disparo no passo)"] += 1
                else:
                    eventos["key-on em canal com KYONB e a mesma amostra"] += 1
                ocupacao[sum(1 for v in k.values() if v) // 8 * 8] += 1
            k[ch] = kk
            if s:
                sa[ch] = s
    total = sum(eventos.values())
    print(f"key-on nos canais da musica: {total}")
    for nome, n in eventos.most_common():
        print(f"  {nome}: {n} ({100.0 * n / max(total, 1):.1f} %)")
    print("canais com KYONB no momento do key-on (faixas de 8):", sorted(ocupacao.items()))
    print("canais usados:", len(k), "de 48")


if __name__ == "__main__":
    main()
