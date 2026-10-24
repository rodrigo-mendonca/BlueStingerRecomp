"""Antes de publicar (docs/PUBLICACAO-GITHUB.md): varre cada commit de <faixa> (ex.: origin/master..master) procurando
texto das falas do jogo (as frases dos pacotes de legenda LOCAIS em mods-padrao/, que ficam fora do Git), caminhos
proibidos (imagem do jogo, BIOS, saves, dados extraidos, pacotes de legenda, icone da capa), segredos e arquivos
grandes. Sai com 1 se achar algo.

    python tools/dev/varre_publicacao.py origin/master..master   (BS_REPO=<outro repositorio>, opcional)
"""
import os
import re
import subprocess
import sys
import tomllib

AQUI = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REPO = os.environ.get("BS_REPO", AQUI)
os.environ.update({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "safe.directory", "GIT_CONFIG_VALUE_0": "*"})


def git(*a, binary=False):
    r = subprocess.run(["git", "-C", REPO, *a], capture_output=True)
    return r.stdout if binary else r.stdout.decode("utf-8", "replace")


def frases():
    out = set()
    for pac in ("legendas-en", "legendas-pt-br"):
        d = os.path.join(AQUI, "mods-padrao", pac)
        for raiz, _, arqs in os.walk(d):
            for a in arqs:
                p = os.path.join(raiz, a)
                if a.endswith(".toml") and a.startswith("falas"):
                    for f in tomllib.load(open(p, "rb")).get("fala", []):
                        out.add(f["texto"])
                elif a.endswith(".srt"):
                    for bloco in open(p, encoding="utf-8").read().split("\n\n"):
                        ls = bloco.strip().split("\n")
                        if len(ls) >= 3:
                            out.add(" ".join(ls[2:]))
    norm = set()
    for t in out:
        t = " ".join(t.replace("\\n", " ").split())
        # pedacos de 24+ caracteres (o texto em codigo pode estar quebrado de outro jeito)
        for frase in re.split(r"(?<=[.!?])\s+", t):
            if len(frase) >= 24:
                norm.add(frase.lower())
    return norm


PROIBIDOS = re.compile(r"(\.cdi|\.gdi|\.chd|\.iso|dc_boot|dc_flash|bios|BLUESTIN\.\d{3}|\.vms|\.vmu|known_versions\.h|"
                       r"DISTRIBUICAO-COMPILADOR|game-data/|generated/|analysis/|reviews/|mods-padrao/legendas|icone-local/)",
                       re.I)
# os prefixos montados em partes: a propria linha nao pode parecer um segredo para esta varredura
SEGREDO = re.compile("(" + "|".join(["gh" + "p_[A-Za-z0-9]{20,}", "github" + "_pat_", "sk" + "-[A-Za-z0-9]{20,}",
                                      "AK" + "IA[0-9A-Z]{16}", "-----BEGIN [A-Z ]*PRIV" + "ATE KEY"]) + ")")


def main():
    faixa = sys.argv[1]
    fs = frases()
    print("frases de referencia:", len(fs))
    commits = git("rev-list", "--reverse", faixa).split()
    achados = 0
    for c in commits:
        tit = git("log", "-1", "--format=%h %s", c).strip()
        arquivos = git("show", "--name-status", "--format=", c).splitlines()
        for l in arquivos:
            st, *paths = l.split("\t")
            for p in paths:
                if PROIBIDOS.search(p) and st[0] in "AMR":
                    print("  [caminho] %s: %s %s" % (tit, st, p))
                    achados += 1
        diff = git("show", "--format=", "-U0", "--text", "--no-renames", c)
        arq = None
        for l in diff.splitlines():
            if l.startswith("+++ "):
                arq = l[6:]
                continue
            if not l.startswith("+") or l.startswith("+++"):
                continue
            s = " ".join(l[1:].lower().split())
            if SEGREDO.search(l):
                print("  [segredo?] %s: %s: %s" % (tit, arq, l[:120]))
                achados += 1
            for f in fs:
                if f in s:
                    print("  [fala] %s: %s: %s" % (tit, arq, f[:70]))
                    achados += 1
                    break
    # binarios grandes adicionados
    for c in commits:
        for l in git("diff-tree", "-r", "--no-commit-id", "--diff-filter=AM", c).splitlines():
            parts = l.split()
            if len(parts) >= 6:
                blob, path = parts[3], l.split("\t")[1]
                tam = int(git("cat-file", "-s", blob).strip() or 0)
                if tam > 2_000_000:
                    print("  [grande] %s: %s %d bytes" % (git("log", "-1", "--format=%h", c).strip(), path, tam))
                    achados += 1
    print("commits: %d, achados: %d" % (len(commits), achados))
    return 1 if achados else 0


if __name__ == "__main__":
    sys.exit(main())
