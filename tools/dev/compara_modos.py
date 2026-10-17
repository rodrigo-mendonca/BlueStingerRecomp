"""Compara duas execucoes (BS_FRAME_LOG, BS_FILE_LOG, BS_AICA_LOG) por passo de jogo.

  python tools/dev/compara_modos.py <prefixo A> <prefixo B> [--de PASSO] [--ate PASSO]

Cada prefixo P tem P_fr.tsv (quadros: vblank, quadro, entrada, passo), P_fl.tsv (leituras) e,
opcionais, P_au.tsv (audio, BS_AICA_LOG com BS_AICA_LOG_TUDO) e P_st.tsv (estado logico por
passo, BS_STATE_PASSO). Com um roteiro "indice passo" (gameplay assistido, app/assist.h) a
mesma entrada cai no mesmo passo nos dois modos de tempo, entao o quadro exibido no fim de cada
passo e comparavel diretamente. Relata:
  - quadros: passos com o quadro final igual / diferente, e as faixas de passos diferentes;
  - leituras: a sequencia (arquivo, deslocamento, tamanho, SHA-256), sem o VBlank;
  - cargas: os arquivos R*.BIN (salas) em ordem, com o passo em que cada um foi lido;
  - audio: as linhas "fim" (escritas e hash acumulado) do BS_AICA_LOG.
"""
import argparse
import os


def frames(prefix):
    by_step, vb = {}, {}
    with open(prefix + "_fr.tsv") as f:
        next(f)
        for line in f:
            v, h, _e, p = line.rstrip("\n").split("\t")
            by_step[int(p)] = h  # quadro exibido no ultimo VBlank do passo
            vb[int(v)] = int(p)
    return by_step, vb


def reads(prefix, vb):
    out = []
    path = prefix + "_fl.tsv"
    if not os.path.exists(path):
        return out
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            if c[3] != "dado":
                continue
            v = int(c[1])
            out.append((vb.get(v, vb.get(v - 1, -1)), c[4], c[5], c[6], c[7]))
    return out


def state(prefix):
    """BS_STATE_PASSO (P_st.tsv): passo -> (hash das variaveis do jogo, vida, x, y, z), sem o VBlank."""
    path = prefix + "_st.tsv"
    if not os.path.exists(path):
        return None
    out = {}
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            out[int(c[0])] = tuple(c[2:])
    return out


def ranges(xs):
    out, start, prev = [], None, None
    for x in xs:
        if start is None:
            start = prev = x
        elif x == prev + 1:
            prev = x
        else:
            out.append((start, prev))
            start = prev = x
    if start is not None:
        out.append((start, prev))
    return out


def audio(prefix, vb, last):
    """Escritas na AICA ate o passo `last` (BS_AICA_LOG_TUDO), sem as copias de guarda do anel do
    stream de video (8 palavras depois do fim de cada canal, regravadas a cada VBlank enquanto o
    video enche o buffer; mesmo filtro de tools/dist/test_n2b2.ps1). Sem o log completo: as linhas
    de hash acumulado."""
    path = prefix + "_au.tsv"
    if not os.path.exists(path):
        return None
    out, full = [], False
    with open(path) as f:
        for line in f:
            if not line.startswith("w\t"):
                continue
            full = True
            _, a, val, size, v = line.rstrip("\n").split("\t")
            addr = int(a, 16)
            if val != "41474553" and (0x009FFFC0 <= addr <= 0x009FFFDC or 0x009FBF80 <= addr <= 0x009FBF9C):
                continue
            if vb.get(int(v), 0) <= last:
                out.append((a, val, size))
    if not full:
        with open(path) as f:
            return [l.strip() for l in f if l.split("\t")[-1].strip() == "fim"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--de", type=int, default=0)
    ap.add_argument("--ate", type=int, default=None)
    args = ap.parse_args()
    fa, vba = frames(args.a)
    fb, vbb = frames(args.b)
    last = min(max(fa), max(fb)) - 1  # o ultimo passo pode estar incompleto
    if args.ate is not None:
        last = min(last, args.ate)
    steps = [s for s in range(args.de, last + 1) if s in fa and s in fb]
    diff = [s for s in steps if fa[s] != fb[s]]
    # todo passo da faixa tem de estar nos dois logs (um buraco nao pode passar como igual)
    missing = [s for s in range(args.de, last + 1) if s not in fa or s not in fb]
    print(f"quadros: passos {args.de}..{last}: {len(steps) - len(diff)} iguais, {len(diff)} diferentes, "
          f"{len(missing)} ausentes")
    for a, b in ranges(diff)[:30]:
        print(f"  diferentes: passos {a}..{b}")

    ra, rb = reads(args.a, vba), reads(args.b, vbb)
    ka = [r[1:] for r in ra if r[0] <= last]
    kb = [r[1:] for r in rb if r[0] <= last]
    same = ka == kb
    print(f"leituras: {len(ka)} x {len(kb)} blocos ate o passo {last}: {'mesma sequencia' if same else 'DIFERENTES'}")
    if not same:
        for i, (x, y) in enumerate(zip(ka, kb)):
            if x != y:
                print(f"  primeira diferenca no bloco {i}: {x} x {y}")
                break
    salas_a = [(r[0], r[1]) for r in ra if r[1].split("/")[-1].startswith("R") and r[1].endswith(".BIN") and r[2] == "0"]
    salas_b = [(r[0], r[1]) for r in rb if r[1].split("/")[-1].startswith("R") and r[1].endswith(".BIN") and r[2] == "0"]
    print("cargas de sala (passo, arquivo):")
    for i in range(max(len(salas_a), len(salas_b))):
        x = salas_a[i] if i < len(salas_a) else None
        y = salas_b[i] if i < len(salas_b) else None
        print(f"  {x}  |  {y}")
    verdict = {"quadros": not diff and not missing and bool(steps), "leituras": same and bool(ka),
               "cargas": [x for x in salas_a if x[0] <= last] == [x for x in salas_b if x[0] <= last]}
    sa, sb = state(args.a), state(args.b)
    if sa is not None and sb is not None:
        # amostras esperadas: os multiplos do intervalo (BS_STATE_PASSO_CADA) dentro da faixa
        every = min((y - x for x, y in zip(sorted(sa), sorted(sa)[1:])), default=10)
        expected = [s for s in range(max(args.de, every), last + 1) if s % every == 0]
        absent = [s for s in expected if s not in sa or s not in sb]
        common = [s for s in expected if s in sa and s in sb]
        bad = [s for s in common if sa[s] != sb[s]]
        print(f"estado logico (BS_STATE_PASSO, a cada {every}): {len(common)} passos amostrados, "
              f"{len(common) - len(bad)} iguais, {len(bad)} diferentes, {len(absent)} ausentes")
        for s in bad[:5]:
            print(f"  passo {s}: {sa[s]} x {sb[s]}")
        verdict["estado"] = not bad and not absent and bool(common)

    aa, ab = audio(args.a, vba, last), audio(args.b, vbb, last)
    if aa is not None and ab is not None:
        if aa and isinstance(aa[0], tuple):
            print(f"audio: {len(aa)} x {len(ab)} escritas na AICA (sem as copias de guarda): "
                  f"{'mesma sequencia' if aa == ab else 'DIFERENTES'}")
            verdict["audio"] = aa == ab
            if aa != ab:
                for i, (x, y) in enumerate(zip(aa, ab)):
                    if x != y:
                        print(f"  primeira diferenca na escrita {i}: {x} x {y}")
                        break
        else:
            print(f"audio (so hash acumulado, inclui as copias de guarda): {aa} x {ab}")
    # linha para os testes (tools/dist/test_assistido.ps1)
    print("RESULTADO ultimo_passo=%d %s" % (last, " ".join(f"{k}={'ok' if v else 'FALHA'}" for k, v in verdict.items())))


if __name__ == "__main__":
    main()
