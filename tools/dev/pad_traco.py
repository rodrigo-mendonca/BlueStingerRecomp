"""Traco de entrada por passo (BS_PAD_TRACE, N3-0): comparacao e conferencias.

  python tools/dev/pad_traco.py compara <a.tsv> <b.tsv> [--ate N] [--com-tempo] [--ignora col,col]
      Compara passo a passo (por padrao sem as colunas de tempo vblank/leituras, que mudam entre
      modos de tempo). Sai 1 se houver diferenca, passo so num lado ou cabecalhos diferentes.
  python tools/dev/pad_traco.py limiares <traco.tsv> <limiares.tsv>
      Confere, para cada ponto de fronteira, o PDS depois do pos-processamento do jogo (zona morta
      estrita -20 < v < 20 -> 0) e a palavra de acoes (direcional so com v < -48 ou v > 48; gatilho
      como botao com byte >= 128: R 0x10000, L 0x20000).
  python tools/dev/pad_traco.py conexao <traco.tsv> <conexao.tsv>
      Confere desconexao com A apertado, reconexao neutra e reconexao com A apertado.
  python tools/dev/pad_traco.py boot <traco.tsv>
      Porta vazia desde o boot, conexao no passo 60, START no 101.
  python tools/dev/pad_traco.py softreset <traco.tsv> <saida.txt> <saida do controle.txt>
      A+B+X+Y e START: o jogo acende o pedido de soft reset e chama o menu do BIOS (syscall).

Leitura do tempo: o estado do roteiro no passo s e amostrado no VBlank de s, consumido pelo
servidor no passo s+1 e aparece no traco (inicio do passo) em s+2. As conferencias olham uma
janela no meio de cada estado.
"""
import sys

TIME_COLS = {"vblank", "leituras"}


def load(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        head = f.readline().rstrip("\n").split("\t")
        rows = {}
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) != len(head):
                continue
            rows[int(c[0])] = dict(zip(head, c))
    return head, rows


def s16(v):
    return int(v)


def compara(a, b, until=None, with_time=False, ignore=()):
    ha, ra = load(a)
    hb, rb = load(b)
    cols = [c for c in ha if c != "passo" and (with_time or c not in TIME_COLS) and c not in ignore]
    steps = sorted(set(ra) & set(rb))
    if until is not None:
        steps = [s for s in steps if s <= until]
    diff = 0
    first = None
    for s in steps:
        bad = [c for c in cols if ra[s].get(c) != rb[s].get(c)]
        if bad:
            diff += 1
            if first is None:
                first = (s, bad, ra[s], rb[s])
    only = len(set(ra) ^ set(rb))
    print(f"passos comparados {len(steps)}, diferentes {diff}, so num lado {only}")
    if first:
        s, bad, x, y = first
        print(f"primeira diferenca no passo {s}: " + ", ".join(f"{c} {x.get(c)} x {y.get(c)}" for c in bad))
    return 0 if diff == 0 and only == 0 and steps and ha == hb else 1


def window(rows, start, n=8):
    return [rows[s] for s in range(start + 3, start + n) if s in rows]


def limiares(trace, spec):
    _, rows = load(trace)
    fails = 0
    with open(spec, encoding="utf-8") as f:
        f.readline()
        cases = [l.split() for l in f if l.strip()]
    for field, byte, step in cases:
        b, step = int(byte), int(step)
        w = window(rows, step)
        if not w:
            print(f"FALHA {field}={b}: sem passos no traco"); fails += 1; continue
        ok = True
        for r in w:
            on, a0 = int(r["on"], 16), int(r["acao0"], 16)
            if field in ("AX", "AY"):
                v = b - 128
                post = 0 if -20 < v < 20 else v
                got = s16(r["x1" if field == "AX" else "y1"])
                low, high = (0x40, 0x80) if field == "AX" else (0x10, 0x20)
                want_dir = (low if post < -48 else 0) | (high if post > 48 else 0)
                ok &= got == post and (a0 & (low | high)) == want_dir and on == 0
            else:
                bit = 0x10000 if field == "RT" else 0x20000
                want = bit if b >= 128 else 0
                rl = int(r["r" if field == "RT" else "l"])
                ok &= (on & 0x30000) == want and (a0 & 0x30000) == want and rl == b
        print(f"{'ok' if ok else 'FALHA'} {field}={b} (v={b - 128 if field in ('AX', 'AY') else b}) em {len(w)} passos: "
              f"x1 {w[0]['x1']} y1 {w[0]['y1']} on {w[0]['on']} acao0 {w[0]['acao0']}")
        fails += 0 if ok else 1
    return 1 if fails else 0


def first_step(rows, start, pred, limit=40):
    for s in range(start, start + limit):
        if s in rows and pred(rows[s]):
            return s
    return None


def conexao(trace, spec):
    _, rows = load(trace)
    with open(spec, encoding="utf-8") as f:
        f.readline()
        at = {k: int(v) for k, v in (l.split() for l in f if l.strip())}
    absent = lambda r: r["id"] == "00000000" and r["nome"] == "(no device)" and r["off"] == "FFFFFFFF" and r["on"] == "00000000"
    present = lambda r: r["id"] == "000F06FE" and r["nome"].startswith("Dreamcast Controller")
    checks = []
    checks.append(("segurando A", all(int(r["on"], 16) == 4 and present(r) for r in window(rows, at["segurando_A"]))))
    d = first_step(rows, at["desconecta_com_A"], absent)
    checks.append((f"desconecta com A apertado: porta vazia no passo {d}", d is not None and d - at["desconecta_com_A"] <= 3))
    checks.append(("desconectado ate reconectar", all(absent(rows[s]) for s in range(d, at["reconecta_neutro"] + 1) if s in rows) if d else False))
    rn = first_step(rows, at["reconecta_neutro"], present)
    checks.append((f"reconecta neutro: controle no passo {rn}, sem botao", rn is not None and int(rows[rn]["on"], 16) == 0 and int(rows[rn]["press"], 16) == 0))
    d2 = first_step(rows, at["desconecta"], absent)
    checks.append((f"desconecta de novo no passo {d2}", d2 is not None))
    ra = first_step(rows, at["reconecta_com_A"], present)
    checks.append((f"reconecta com A: controle no passo {ra}, A em on e press no primeiro passo",
                   ra is not None and int(rows[ra]["on"], 16) == 4 and int(rows[ra]["press"], 16) == 4))
    checks.append(("A continua apertado, sem nova borda",
                   ra is not None and all(int(rows[s]["on"], 16) == 4 and int(rows[s]["press"], 16) == 0 for s in range(ra + 1, ra + 6) if s in rows)))
    for name, ok in checks:
        print(("ok " if ok else "FALHA ") + name)
    return 0 if all(ok for _, ok in checks) else 1


def boot(trace):
    _, rows = load(trace)
    absent = lambda r: r["id"] == "00000000" and r["nome"] == "(no device)"
    early = [rows[s] for s in range(10, 58) if s in rows]
    c = first_step(rows, 60, lambda r: r["id"] == "000F06FE")
    # o titulo consome a borda (o jogo escreve em press): confere o botao segurado
    st = first_step(rows, 101, lambda r: int(r["on"], 16) & 0x8, 10)
    checks = [("porta vazia desde o boot", bool(early) and all(absent(r) for r in early)),
              (f"controle aparece no passo {c}", c is not None and c - 60 <= 3),
              (f"START visto em on no passo {st}", st is not None)]
    for name, ok in checks:
        print(("ok " if ok else "FALHA ") + name)
    return 0 if all(ok for _, ok in checks) else 1


def softreset(trace, out, control_out):
    # o pedido (bit 2 de 8C24F878) e atendido no mesmo passo: o traco do inicio do passo seguinte nao
    # chega a existir. Prova: A+B+X+Y segurados no ultimo passo do traco, a syscall do menu do BIOS
    # logo depois do START e o controle (A+B+X+Y sem START) sem a syscall.
    _, rows = load(trace)
    last = max(rows) if rows else None
    held = last is not None and (int(rows[last]["on"], 16) & 0x0606) == 0x0606
    with open(out, encoding="utf-8", errors="replace") as f:
        text = f.read()
    with open(control_out, encoding="utf-8", errors="replace") as f:
        control = f.read()
    sig = "syscall do BIOS ainda nao implementada em PC=8C001008"
    checks = [(f"A+B+X+Y segurados no ultimo passo ({last})", held and 40 <= last <= 43),
              ("START com A+B+X+Y: o jogo chama o menu do BIOS (syscall 8C001008, sem HLE)", sig in text),
              ("controle sem START: sem a syscall", sig not in control and "VBlanks:" in control)]
    for name, ok in checks:
        print(("ok " if ok else "FALHA ") + name)
    return 0 if all(ok for _, ok in checks) else 1

def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == "compara":
        until = int(a[a.index("--ate") + 1]) if "--ate" in a else None
        ignore = a[a.index("--ignora") + 1].split(",") if "--ignora" in a else ()
        sys.exit(compara(a[1], a[2], until, "--com-tempo" in a, ignore))
    if cmd == "limiares":
        sys.exit(limiares(a[1], a[2]))
    if cmd == "conexao":
        sys.exit(conexao(a[1], a[2]))
    if cmd == "boot":
        sys.exit(boot(a[1]))
    if cmd == "softreset":
        sys.exit(softreset(a[1], a[2], a[3]))
    sys.exit(__doc__)


if __name__ == "__main__":
    main()
