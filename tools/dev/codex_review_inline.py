"""Revisao do Codex com o material no proprio prompt (quando o sandbox do Codex nao consegue ler o repositorio, como
no erro helper_sandbox_lock_failed/SetNamedSecurityInfoW). Junta o prompt padrao (codex_review_prompt.md), o escopo, o
diff dos arquivos pedidos e anexos de evidencia (disassembly, medidas) e grava a resposta em reviews/codex-<data>.md.

    python tools/dev/codex_review_inline.py --escopo "<texto>" --arquivos a.cpp,b.h [--anexo arq.txt ...] [--base HEAD]
"""
import argparse
import datetime
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--escopo", required=True)
    ap.add_argument("--arquivos", required=True)
    ap.add_argument("--anexo", action="append", default=[])
    ap.add_argument("--base", default="HEAD")
    a = ap.parse_args()
    arqs = [x for x in a.arquivos.split(",") if x]
    git = ["git", "-c", "safe.directory=*", "-C", REPO]
    diff = subprocess.run(git + ["diff", "-U12", a.base, "--"] + arqs, capture_output=True, text=True, encoding="utf-8").stdout
    stat = subprocess.run(git + ["diff", "--stat", a.base, "--"] + arqs, capture_output=True, text=True, encoding="utf-8").stdout
    base = open(os.path.join(REPO, "tools", "dev", "codex_review_prompt.md"), encoding="utf-8").read()
    base = base.replace("{{SCOPE}}", a.escopo).replace("{{BASE}}", a.base).replace("{{DIFFSTAT}}", stat.strip())
    base = base.replace("{{UNTRACKED}}", "(nao se aplica)")
    anexos = "".join(f"\n\n### Anexo: {os.path.basename(p)}\n```\n{open(p, encoding='utf-8', errors='replace').read()}\n```"
                     for p in a.anexo)
    prompt = (base + "\n\n## Material desta revisao (no prompt: o sandbox nao consegue ler o repositorio)\n"
              "Nao tente executar comandos nem ler arquivos: tudo o que voce precisa esta abaixo. Achados so com evidencia "
              "deste material; o que depender de outro arquivo, marque como nao verificado.\n\n### Diff\n```diff\n" + diff +
              "\n```" + anexos + "\n")
    out = os.path.join(REPO, "reviews", "codex-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".md")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r = subprocess.run(["codex", "exec", "--sandbox", "read-only", "--ephemeral", "-C", REPO, "-o", out, "-"], input=prompt,
                       text=True, encoding="utf-8", capture_output=True, shell=True)
    if r.returncode != 0 or not os.path.exists(out):
        raise SystemExit(f"codex falhou ({r.returncode}): {r.stderr[-600:]}")
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
