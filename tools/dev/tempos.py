"""Onde o tempo de build e testes vai (so leitura): historico das regressoes e unidades de compilacao.

  python tools/dev/tempos.py [--estado <pasta da regressao>] [--build build/msvc-release] [--n 10]

- Regressoes: cada execucao do tools/dev/regressao.ps1 no regressao.log (etapas, horas, falhas) e as N
  etapas mais caras (mediana entre execucoes).
- Build: as N saidas mais caras do .ninja_log (ultima compilacao de cada) e a soma de CPU por pasta.
"""
import argparse
import collections
import os
import re
from pathlib import Path


def regressoes(estado: Path, n: int) -> None:
    log = estado / "regressao.log"
    if not log.exists():
        print(f"sem {log}")
        return
    runs, cur = [], None
    for line in log.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        m = re.match(r"== regressao (\S+)", line)
        if m:
            cur = {"ini": m.group(1).rstrip(","), "st": []}
            runs.append(cur)
            continue
        m = re.match(r"== (\S+) rc=(-?\d+) \((\d+)s\)", line)
        if m and cur is not None:
            cur["st"].append((m.group(1), int(m.group(2)), int(m.group(3))))
    print(f"== regressoes ({log})")
    for r in runs:
        tot = sum(s for _, _, s in r["st"])
        falhas = [x for x, rc, _ in r["st"] if rc]
        print(f"  {r['ini']}: {len(r['st'])} etapas, {tot / 3600:.2f} h" + (f", falhas {falhas}" if falhas else ""))
    agg = collections.defaultdict(list)
    for r in runs:
        for name, _, s in r["st"]:
            agg[name].append(s)
    med = {k: sorted(v)[len(v) // 2] for k, v in agg.items()}
    total = sum(med.values()) or 1
    print(f"  top {n} etapas (mediana; soma das medianas {total / 3600:.2f} h):")
    for k, v in sorted(med.items(), key=lambda x: -x[1])[:n]:
        print(f"    {k:16} {v / 60:6.1f} min  {100 * v / total:4.1f}%  (n={len(agg[k])}, max {max(agg[k]) / 60:.1f})")


def build(bdir: Path, n: int) -> None:
    log = bdir / ".ninja_log"
    if not log.exists():
        print(f"sem {log}")
        return
    last = {}
    for line in log.read_text(errors="replace").splitlines():
        if line.startswith("#"):
            continue
        c = line.split("\t")
        if len(c) >= 4:
            last[c[3]] = (int(c[1]) - int(c[0])) / 1000
    objs = {o: t for o, t in last.items() if o.endswith((".obj", ".exe", ".lib", ".dll"))}
    print(f"== build {bdir}: {len(objs)} saidas, CPU somada {sum(objs.values()):.0f} s (ultima compilacao de cada)")
    por = collections.Counter()
    for o, t in objs.items():
        por[o.split("/")[0] if "/" in o else "(raiz)"] += t
    print("  por pasta: " + ", ".join(f"{k} {v:.0f}s" for k, v in por.most_common(6)))
    for o, t in sorted(objs.items(), key=lambda x: -x[1])[:n]:
        print(f"    {t:7.1f}s {o}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--estado", default=os.path.join(os.environ.get("LOCALAPPDATA", ""), "BlueStingerRecomp", "regressao"))
    ap.add_argument("--build", action="append")
    ap.add_argument("--n", type=int, default=10)
    a = ap.parse_args()
    regressoes(Path(a.estado), a.n)
    for b in a.build or ["build/msvc-release", "build/msvc-debug"]:
        build(Path(b), a.n)


if __name__ == "__main__":
    main()
