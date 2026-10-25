"""N8.1: resumo do BS_INTC_TRACE (runtime/debug/intc_trace.h) de uma ou mais execucoes: entregas (E) por origem e por
INTEVT, bits acesos (H) por fonte, fusoes, limpezas (A), leituras de relogio (C) e operacoes do cartao (L). Serve para
medir, antes de mudar a cadencia, quantas entregas dependem do quantum de ciclos (`cadencia`) e de que fonte vieram.

    python tools/dev/n8_intc.py <intc.tsv> [<intc.tsv> ...] [--json <f>]
"""
import argparse
import collections
import json
import sys

CAB = "pos\tpasso\tvblank\tciclos\ttipo\tfonte\ta\tb\tistnrm\timl6nrm\timl4nrm\timl2nrm\tn"


def ler(path):
    with open(path, encoding="utf-8") as f:
        ls = f.read().split("\n")
    if ls and ls[-1] == "":
        ls.pop()
    if len(ls) < 2 or not ls[0].startswith("# ") or ls[1] != CAB:
        raise SystemExit("%s: sem o cabecalho do BS_INTC_TRACE" % path)
    for i, l in enumerate(ls[2:], 3):
        f = l.split("\t")
        if len(f) != 13:
            raise SystemExit("%s: linha %d com %d colunas" % (path, i, len(f)))
        yield f


def resumo(path):
    r = {"E": collections.Counter(), "E_intevt": collections.Counter(), "H": collections.Counter(), "fusoes": 0,
         "A": collections.Counter(), "C": collections.Counter(), "C_linhas": 0, "L": collections.Counter(),
         "cadencia_bits": collections.Counter(), "S": 0, "S_entregas": 0, "S_eventos": 0, "S_blocos": 0,
         "H_origem": collections.Counter()}
    nomes = {0: "fora", 14: "cadencia", 15: "forcado", 16: "ldc_sr", 17: "hardware", 18: "sleep"}
    ultimo_h = None  # bits acesos pelo ultimo H antes de cada entrega (a fonte provavel)
    for f in ler(path):
        t, src = f[4], f[5]
        a, b = int(f[6], 16), int(f[7], 16)
        if t == "E":
            r["E"][src] += 1
            r["E_intevt"]["%s %03X" % (src, a)] += 1
            if src == "cadencia" and ultimo_h:
                r["cadencia_bits"]["%s %08X" % ultimo_h] += 1
        elif t == "S":
            r["S"] += 1
            r["S_eventos"] += a
            r["S_entregas"] += b
            r["S_blocos"] += int(f[12])
        elif t == "H":
            r["H"][src] += 1
            r["H_origem"][nomes.get(int(f[12]), f[12])] += 1
            if b & ~a:
                r["fusoes"] += 1
            if a:
                ultimo_h = (src, a)
        elif t == "A":
            r["A"][src] += 1
        elif t == "C":
            r["C"][src] += int(f[12])
            r["C_linhas"] += 1
        elif t == "L":
            r["L"][src + " " + f[6]] += 1
    return {k: (dict(v) if isinstance(v, collections.Counter) else v) for k, v in r.items()}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("traces", nargs="+")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    out = {}
    for p in a.traces:
        out[p] = resumo(p)
        r = out[p]
        print("== %s" % p)
        print("  entregas por origem: %s" % r["E"])
        print("  entregas por origem e INTEVT: %s" % r["E_intevt"])
        print("  na cadencia, ultimo H antes: %s" % dict(sorted(r["cadencia_bits"].items(), key=lambda x: -x[1])[:8]))
        print("  H por fonte: %s; por origem do atendimento: %s; fusoes (bit pedido ja aceso): %d" % (r["H"], r["H_origem"], r["fusoes"]))
        print("  atendimentos da cadencia com efeito: %d (eventos do Holly %d, entregas %d, blocos do GD-ROM %d)" %
              (r["S"], r["S_eventos"], r["S_entregas"], r["S_blocos"]))
        print("  A: %s; C (leituras): %s em %d linhas; L: %s" % (r["A"], r["C"], r["C_linhas"], r["L"]))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
