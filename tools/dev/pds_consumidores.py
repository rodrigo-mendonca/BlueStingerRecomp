"""Consumidores do PDS_PERIPHERAL no 1ST_READ.BIN (N3-0, docs/N3-0-RELATORIO.md).

  python tools/dev/pds_consumidores.py <bs-disasm.exe> <1ST_READ.BIN> <sdk_funcs.tsv> <saida.tsv> [--sem-juncao]

Varredura estatica de todas as funcoes cujo codigo carrega, por literal de pool:
  - a variavel 8C24CCB0 (o PDS* que o jogo escolhe em 8C064878)          -> origem "ptr"
  - um endereco da tabela 8C2D8970..8C2D8EB3 (unidades, estado, modo)    -> origem "tabela"
  - o getter 8C15C668 (pdGetPeripheral; o r0 da chamada e o ponteiro)    -> origem "getter"
Dentro de cada funcao, rastreia os registradores que contem o ponteiro (carga @var, copia mov,
retorno do getter) e lista os acessos @(disp,reg) aos campos. Uma copia do ponteiro para a memoria
ou a pilha, ou o ponteiro em r4-r7 numa chamada, e marcada "escapa": o consumidor real pode estar no
chamado (conferir a mao). Rastreio linear por funcao, sem fluxo: um rotulo de desvio desfaz o
conhecimento dos registradores (conservador para "escapa", pode perder acessos depois de desvios,
que sao listados como "possivel_perda"). --sem-juncao mantem o conhecimento nas juncoes (otimista):
a diferenca entre as duas saidas e o que precisa de revisao manual (docs/N3-0-RELATORIO.md, secao 3).
"""
import bisect
import csv
import re
import struct
import subprocess
import sys

FIELDS = {0: "id", 4: "support", 8: "on", 12: "off", 16: "press", 20: "release", 24: "r", 26: "l", 28: "x1",
          30: "y1", 32: "x2", 34: "y2", 36: "name", 40: "extend", 44: "old", 48: "info"}
VAR, GETTER = 0x8C24CCB0, 0x8C15C668
TABLE_LO, TABLE_HI = 0x8C2D8970, 0x8C2D8EB4
BASE = 0x8C010000


def main():
    dis, binpath, sdk, out = sys.argv[1:5]
    keep = "--sem-juncao" in sys.argv
    data = open(binpath, "rb").read()
    funcs, layer = [], {}
    for r in csv.DictReader(open(sdk, encoding="utf-8"), delimiter="\t"):
        a = int(r["funcao"], 16)
        funcs.append(a)
        layer[a] = r["camada"]
    funcs.sort()

    def owner(pc):
        i = bisect.bisect_right(funcs, pc) - 1
        return funcs[i] if i >= 0 else None

    # literais de pool de interesse
    cand = {}
    for o in range(0, len(data) - 3, 4):
        v = struct.unpack_from("<I", data, o)[0]
        if v == VAR or v == GETTER or TABLE_LO <= v < TABLE_HI:
            f = owner(BASE + o)
            if f:
                cand.setdefault(f, set()).add(v)

    rows = []
    for f in sorted(cand):
        i = bisect.bisect_right(funcs, f)
        end = funcs[i] if i < len(funcs) else f + 0x400
        text = subprocess.run([dis, binpath, hex(BASE), hex(f), str((end - f) // 2)], capture_output=True, text=True).stdout
        ptr, var_regs, getter_regs = set(), set(), set()
        reads, writes, escapes, labels = [], [], [], 0
        origins = set()
        table_refs = []
        pending_getter = False
        targets = set()
        lines = [l.split(None, 2) for l in text.splitlines() if len(l.split(None, 2)) == 3]
        for pc, _, ins in lines:  # alvos de desvio dentro da funcao
            m = re.match(r"(?:bra|bt|bf|bt/s|bf/s|bsr) 0x([0-9a-f]+)", ins)
            if m:
                targets.add(int(m.group(1), 16))
        for pc_s, _, ins in lines:
            pc = int(pc_s, 16)
            if pc in targets and ptr and not keep:
                labels += 1
                ptr = set()  # sem fluxo: um ponto de juncao desfaz o conhecimento
            m = re.match(r"mov\.l 0x([0-9a-f]+),(r\d+)", ins)
            if m:
                lit = struct.unpack_from("<I", data, int(m.group(1), 16) - BASE)[0]
                reg = m.group(2)
                ptr.discard(reg); var_regs.discard(reg); getter_regs.discard(reg)
                if lit == VAR:
                    var_regs.add(reg); origins.add("ptr")
                elif lit == GETTER:
                    getter_regs.add(reg); origins.add("getter")
                elif TABLE_LO <= lit < TABLE_HI:
                    origins.add("tabela"); table_refs.append(f"{pc:08X}:{lit:08X}")
                continue
            m = re.match(r"jsr @(r\d+)", ins)
            if m:
                if m.group(1) in getter_regs:
                    pending_getter = True
                if ptr & {"r4", "r5", "r6", "r7"}:
                    escapes.append(f"{pc:08X} argumento ({','.join(sorted(ptr & {'r4', 'r5', 'r6', 'r7'}))})")
                continue
            if pending_getter and not ins.startswith("nop") and "jsr" not in ins:
                # depois do slot de atraso: r0 e o PDS*
                pending_getter = False
                ptr.add("r0")
            m = re.match(r"mov\.l @(r\d+),(r\d+)$", ins)
            if m and m.group(1) in var_regs:
                ptr.add(m.group(2)); continue
            m = re.match(r"mov (r\d+),(r\d+)$", ins)
            if m:
                if m.group(1) in ptr:
                    ptr.add(m.group(2))
                else:
                    ptr.discard(m.group(2))
                continue
            m = re.search(r"@\((\d+),(r\d+)\)", ins)
            if m and m.group(2) in ptr:
                d = int(m.group(1))
                name = FIELDS.get(d, f"+{d}")
                if re.match(r"mov\.[bwl] r\d+,@", ins):
                    src = re.match(r"mov\.[bwl] (r\d+),", ins).group(1)
                    writes.append(name)
                    if src in ptr:
                        escapes.append(f"{pc:08X} ponteiro gravado em +{d}")
                else:
                    reads.append(name)
                continue
            m = re.match(r"mov\.[bwl] @(r\d+),(r\d+)$", ins)
            if m and m.group(1) in ptr:
                reads.append("id"); continue
            m = re.match(r"mov\.l (r\d+),@(-?r\d+|-r15|\(.*\))", ins)
            if m and m.group(1) in ptr:
                escapes.append(f"{pc:08X} ponteiro gravado na memoria ({ins})")
            m = re.match(r"\S+ \S*,(r\d+)$", ins)
            if m and m.group(1) in ptr and not ins.startswith(("cmp", "tst")):
                ptr.discard(m.group(1))
        def uniq(xs):
            return ",".join(sorted(set(xs), key=lambda n: list(FIELDS.values()).index(n) if n in FIELDS.values() else 99))
        rows.append([f"{f:08X}", layer.get(f, "?"), ",".join(sorted(origins)), uniq(reads), uniq(writes),
                     " | ".join(escapes) or "-", str(labels) if labels else "-", " ".join(table_refs) or "-"])
    with open(out, "w", encoding="utf-8", newline="\n") as fo:
        fo.write("funcao\tcamada\torigem\tle\tescreve\tescapa\tpossivel_perda\treferencias_tabela\n")
        for r in rows:
            fo.write("\t".join(r) + "\n")
    print(f"{len(rows)} funcoes -> {out}")


if __name__ == "__main__":
    main()
