"""N6 fase F: compara, nota a nota, os key-on da musica no Flycast (n6_oraculo.py --musica --ca, depois de um
'injetado' ou de um 'mus-tocar') com as vozes do sintetizador nativo (bs-musica ... <eventos.tsv>, linhas 'voz').

Registradores: cada key-on do oraculo tem uma voz nativa com a mesma amostra (relativa ao banco; a base do banco na
RAM de som sai da diferenca mais comum), OCT, FNS, TL, DIPAN e DISDL? Tempo: o deslocamento global e o erro por janela
de 10 s (TMU0 do oraculo, P0/64 = 781 250 Hz; chamadas de 4 ms no nativo). Os key-on dos efeitos no meio do oraculo
ficam de fora pela amostra (fora do banco da musica).

    python tools/dev/n6_musica_compara.py <oraculo.tsv> <eventos.tsv>
"""
import argparse
import bisect
import collections
import re
import sys

TMU_HZ = 781250.0
RE_KEYON = re.compile(r"c(\d+):k1 tl(\d+) sdl(\d+) pan(\d+) \[sa=([0-9A-F]+) [^\]]*?oct=(-?\d+) fns=(\d+)")
RE_TMU = re.compile(r"tmu=([0-9A-F]{8})/")


def oraculo(caminho):
    """Key-on do oraculo depois do inicio da musica: (segundos desde o 1o, sa, oct, fns, tl, linha)"""
    ini, out = False, []
    for linha in open(caminho, encoding="utf-8", errors="replace"):
        c = linha.rstrip("\n").split("\t")
        if len(c) < 4:
            continue
        if c[2] in ("injetado", "mus-tocar"):
            ini = True
            continue
        if not ini or c[2] != "aica" or "k1" not in c[3]:
            continue
        tm = RE_TMU.search(c[3])
        for m in RE_KEYON.finditer(c[3]):  # uma linha por leitura da AICA: varios canais com key-on
            ca = re.search(r"ca=(\d+)", c[3][m.end():].split("]")[0])
            # [tmu, sa, oct, fns, tl, passo, ca, dipan, sdl]
            out.append([int(tm.group(1), 16) if tm else 0, int(m.group(5), 16), int(m.group(6)), int(m.group(7)),
                        int(m.group(2)), int(c[0]), int(ca.group(1)) if ca else None, int(m.group(4)), int(m.group(3))])
    return out


def nativo(caminho):
    out = []
    for linha in open(caminho, encoding="utf-8"):
        c = linha.rstrip("\n").split("\t")
        if len(c) >= 8 and c[2] == "voz":
            try:  # linha cortada (arquivo ainda sendo escrito pelo jogo): fica de fora
                kv = dict(x.split("=") for x in c[7].split())
                int(kv["sdl"] if "sdl" in kv else 0), int(c[4])
            except (ValueError, KeyError):
                continue
            out.append([int(c[0]), int(kv["sa"], 16), int(kv["oct"]), int(kv["fns"]), int(kv["tl"]), int(c[4]), int(kv.get("pan", -1)), int(kv.get("sdl", -1))])
    return out


def tempo(o, n, base):
    """O instante de cada key-on no oraculo (a leitura, TMU0) contra a voz nativa de mesma amostra, tom e TL mais proxima,
    depois do deslocamento global (o mais comum entre as diferencas, em passos de 4 ms). A leitura e uma vez por passo
    (~33 ms): o erro absoluto e dessa ordem; o que importa e a tendencia por janela de 10 s (deriva). O `ca` (posicao da
    amostra na leitura) nao serve para refinar: com laco e com o fim da amostra ele nao da o tempo desde o key-on."""
    t0 = o[0][0]
    por = collections.defaultdict(list)
    for y in n:
        por[(y[1], y[2], y[3], y[4])].append(y[0] * 0.004)
    est = [(((t0 - x[0]) & 0xFFFFFFFF) / TMU_HZ, (x[1] - base, x[2], x[3], x[4])) for x in o]
    h = collections.Counter()
    for t, k in est[:800]:
        for tn in por.get(k, []):
            if -5 < tn - t < 5:
                h[round((tn - t) / 0.004)] += 1
    if not h:
        return
    off = h.most_common(1)[0][0] * 0.004
    janelas = collections.defaultdict(list)
    for t, k in est:
        lst = por.get(k)
        if not lst:
            continue
        tt = t + off
        i = bisect.bisect_left(lst, tt)
        e = min(((abs(lst[j] - tt), lst[j] - tt) for j in (i - 1, i) if 0 <= j < len(lst)))[1]
        janelas[int(tt // 10)].append(e)
    print(f"tempo (nativo - oraculo, deslocamento {off:.3f} s), mediana por janela de 10 s:")
    for k in sorted(janelas):
        v = sorted(janelas[k])
        print(f"  {k*10:4d}-{k*10+10:4d} s: {len(v):5d} key-on, {v[len(v)//2]*1e3:+7.1f} ms (p25 {v[len(v)//4]*1e3:+7.1f}, "
              f"p75 {v[3*len(v)//4]*1e3:+7.1f})")


def pareia(o, n, base, campos_o, campos_n, janela=0.05):
    """Pareamento um a um (revisao Codex 2026-10-20): cada key-on do oraculo consome no maximo uma voz nativa da mesma
    assinatura, em ordem de tempo, dentro de `janela` s depois do deslocamento LOCAL (mediana por janela de 10 s, que
    absorve o atraso do Dreamcast nos trechos densos e e informado a parte em tempo()). Devolve (pares, faltam, sobram):
    faltam = key-on do oraculo sem voz nativa; sobram = vozes nativas sem key-on do oraculo no trecho coberto (o oraculo
    le a AICA uma vez por passo: notas mais curtas que um passo podem nao aparecer nele)."""
    t0 = o[0][0]
    to = [((t0 - x[0]) & 0xFFFFFFFF) / TMU_HZ for x in o]
    # deslocamento global (o mais comum) e o local por janela de 10 s
    por = collections.defaultdict(list)
    for y in n:
        por[tuple(y[k] for k in campos_n)].append(y[0] * 0.004)
    chave = lambda x: tuple(x[k] - (base if k == 1 else 0) for k in campos_o)
    h = collections.Counter()
    for x, t in list(zip(o, to))[:800]:
        for tn in por.get(chave(x), []):
            if -5 < tn - t < 5:
                h[round((tn - t) / 0.004)] += 1
    off = h.most_common(1)[0][0] * 0.004 if h else 0.0
    loc = collections.defaultdict(list)
    for x, t in zip(o, to):
        lst = por.get(chave(x))
        if lst:
            tt = t + off
            i = bisect.bisect_left(lst, tt)
            d = min(((abs(lst[j] - tt), lst[j] - tt) for j in (i - 1, i) if 0 <= j < len(lst)))[1]
            if abs(d) < 0.3:
                loc[int(tt // 10)].append(d)
    corr = {k: sorted(v)[len(v) // 2] for k, v in loc.items()}
    usados = collections.defaultdict(set)
    pares, faltam = [], []
    fimNativo = max(y[0] for y in n) * 0.004  # o oraculo depois do fim do trecho nativo fica de fora (informado)
    fora = 0
    for x, t in sorted(zip(o, to), key=lambda p: p[1]):
        if t + off > fimNativo + janela:
            fora += 1
            continue
        k = chave(x)
        lst = por.get(k, [])
        tt = t + off
        tt += corr.get(int(tt // 10), 0.0)
        i = bisect.bisect_left(lst, tt - janela)
        achou = None
        while i < len(lst) and lst[i] <= tt + janela:
            if i not in usados[k] and (achou is None or abs(lst[i] - tt) < abs(lst[achou] - tt)):
                achou = i
            i += 1
        if achou is None:
            faltam.append(x)
        else:
            usados[k].add(achou)
            pares.append((t, lst[achou], x))
    tmin, tmax = to[0] + off, min(max(to) + off, fimNativo)
    sobram = sum(1 for k, lst in por.items() for i, tn in enumerate(lst) if tmin <= tn <= tmax and i not in usados[k])
    # inversoes: pares cuja ordem no nativo contradiz a do oraculo por mais de 2 passos de leitura (66 ms)
    pares.sort(key=lambda p: p[0])
    inv = sum(1 for a, b in zip(pares, pares[1:]) if b[1] < a[1] - 0.066)
    return pares, faltam, sobram, inv, fora


def main():
    p = argparse.ArgumentParser()
    p.add_argument("oraculo")
    p.add_argument("eventos")
    a = p.parse_args()
    o, n = oraculo(a.oraculo), nativo(a.eventos)
    if not o or not n:
        sys.exit(f"sem key-on: oraculo {len(o)}, nativo {len(n)}")
    # o tempo nativo a partir da primeira voz (no jogo os ticks contam desde o boot)
    c0 = min(y[0] for y in n)
    for y in n:
        y[0] -= c0
    nsa = {x[1] for x in n}
    base = collections.Counter(x[1] - s for x in o[:200] for s in nsa).most_common(1)[0][0]
    o = [x for x in o if x[1] - base in nsa]  # so as amostras do banco da musica
    print(f"base do banco na RAM de som: {base:06X} (a diferenca mais comum); key-on da musica no oraculo {len(o)}, "
          f"vozes nativas {len(n)}")
    campos = [("amostra e tom (OCT, FNS)", (1, 2, 3), (1, 2, 3)), ("+ TL", (1, 2, 3, 4), (1, 2, 3, 4)),
              ("+ DIPAN", (1, 2, 3, 4, 7), (1, 2, 3, 4, 6)), ("+ DISDL", (1, 2, 3, 4, 7, 8), (1, 2, 3, 4, 6, 7))]
    for nome, io_, in_ in campos:
        pares, faltam, sobram, inv, fora = pareia(o, n, base, io_, in_)
        print(f"um a um, mesma {nome}: {len(pares)}/{len(o) - fora} pareados, {len(faltam)} sem voz nativa, "
              f"{sobram} vozes nativas sem key-on no trecho, {inv} inversoes de ordem"
              + (f" ({fora} key-on do oraculo depois do fim do trecho nativo)" if fora else ""))
        for x in faltam[:4]:
            print(f"  sem par: sa={x[1]-base:06X} oct={x[2]} fns={x[3]} tl={x[4]} pan={x[7]} sdl={x[8]} (passo {x[5]})")
    tempo(o, n, base)


if __name__ == "__main__":
    main()
