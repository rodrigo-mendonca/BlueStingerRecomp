"""Regressao do boot: o codigo recompilado deve repetir o trace do Flycast.

Usa dados locais do usuario (memoria do jogo, fora do git), apontados por
BS_SNAPSHOT_DIR:
    boot_entry.bss       estado na entrada 0x8C010000 (flycast_oracle.py snap)
    boot_flycast3k.tsv   trace de referencia (flycast_oracle.py trace --max-calls 3000)
Sem BS_SNAPSHOT_DIR, sai com 77 (SKIP no CTest).

    python boot_regression.py <bs-diff.exe> <flycast_oracle.py> <aceitas.tsv>
"""
import os
import subprocess
import sys
import tempfile


def main():
    bs_diff, oracle, accepted = sys.argv[1:4]
    snap = os.environ.get("BS_SNAPSHOT_DIR")
    if not snap:
        print("BS_SNAPSHOT_DIR nao definido: teste ignorado")
        return 77
    entry = os.path.join(snap, "boot_entry.bss")
    ref = os.path.join(snap, "boot_flycast3k.tsv")
    for p in (entry, ref):
        if not os.path.isfile(p):
            print(f"falta {p}")
            return 1
    calls = sum(1 for _ in open(ref))
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "recomp.tsv")
        r = subprocess.run([bs_diff, "trace", "0x8C010000", entry, out, str(calls)],
                           capture_output=True, text=True, timeout=300)
        print(r.stdout.strip())
        if r.returncode != 0:
            print(r.stderr[-2000:])
            return 1
        return subprocess.run([sys.executable, "-I", oracle, "first-diff", ref, out, "--accept", accepted]).returncode


if __name__ == "__main__":
    sys.exit(main())
