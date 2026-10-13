"""Compara o decodificador SH-4 do projeto com o Capstone 6 em todos os 65 536 opcodes.

Ferramenta de desenvolvimento opcional (nao faz parte do build nem do produto).

    python -m venv .venv && .venv/Scripts/pip install "capstone>=6"
    bs-disasm --all-opcodes > ours.tsv
    .venv/Scripts/python tools/dev/compare_capstone.py ours.tsv tools/dev/capstone_review.txt

Arquivo de revisao: regras por mnemonico, cada uma com o motivo apos '#':
    alias <nosso> <capstone>   mesmo significado, nome diferente
    capstone-only <mnemonico> [regex]  Capstone decodifica, nos nao (p.ex. SH-2A/SH-4A);
                               o regex opcional precisa casar com os operandos
    ours-only <mnemonico>      nos decodificamos, Capstone nao
Sai com 1 se houver divergencia nao coberta por uma regra.
"""
import re
import struct
import sys

import capstone


def normalize(s: str) -> str:
    s = s.strip().lower()
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r",\s+", ",", s)
    # hex -> decimal para comparar imediatos escritos de formas diferentes
    s = re.sub(r"(?<![\w.])0x([0-9a-f]+)", lambda m: str(int(m.group(1), 16)), s)
    return s


def main() -> int:
    ours = {}
    with open(sys.argv[1], encoding="utf-8") as f:
        for line in f:
            op, _, text = line.rstrip("\n").partition("\t")
            ours[int(op, 16)] = text

    aliases, capstone_only, ours_only = {}, {}, set()
    if len(sys.argv) > 2:
        with open(sys.argv[2], encoding="utf-8") as f:
            for line in f:
                rule = line.partition("#")[0].split()
                if not rule:
                    continue
                if rule[0] == "alias" and len(rule) == 3:
                    aliases[rule[1]] = rule[2]
                elif rule[0] == "capstone-only" and len(rule) in (2, 3):
                    capstone_only.setdefault(rule[1], []).append(re.compile(rule[2] if len(rule) == 3 else ".*"))
                elif rule[0] == "ours-only" and len(rule) == 2:
                    ours_only.add(rule[1])
                else:
                    raise SystemExit(f"regra invalida: {line.strip()}")

    def reviewed(mine: str, theirs: str) -> bool:
        t, _, t_ops = theirs.partition(" ")
        if not mine and any(rx.fullmatch(t_ops) for rx in capstone_only.get(t, [])):
            return True
        if mine and not theirs and mine.split(" ")[0] in ours_only:
            return True
        m, _, rest = mine.partition(" ")
        return m in aliases and normalize(f"{aliases[m]} {rest}") == normalize(theirs)

    md = capstone.Cs(capstone.CS_ARCH_SH,
                     capstone.CS_MODE_SH4 | capstone.CS_MODE_SHFPU | capstone.CS_MODE_LITTLE_ENDIAN)
    mismatches = {}
    for op in range(65536):
        insns = list(md.disasm(struct.pack("<H", op), 0x8C010000))
        theirs = f"{insns[0].mnemonic} {insns[0].op_str}".strip() if insns else ""
        mine = ours.get(op, "")
        if normalize(mine) != normalize(theirs):
            mismatches[op] = (mine, theirs)

    unreviewed = {k: v for k, v in mismatches.items() if not reviewed(*v)}
    # agrupa pelo mnemonico para um relatorio curto
    groups = {}
    for op, (mine, theirs) in unreviewed.items():
        key = (mine.split(" ")[0] or "<invalido>", theirs.split(" ")[0] or "<invalido>")
        groups.setdefault(key, []).append(op)
    for (m, t), ops in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        ex = ops[0]
        print(f"{len(ops):6d}  nosso={m:10s} capstone={t:10s}  ex: {ex:04x}  '{ours.get(ex, '')}' vs "
              f"'{mismatches[ex][1]}'")
    print(f"divergencias: {len(mismatches)}  revisadas: {len(mismatches) - len(unreviewed)}  "
          f"nao revisadas: {len(unreviewed)}")
    return 1 if unreviewed else 0


if __name__ == "__main__":
    sys.exit(main())
