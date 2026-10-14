"""Regressao do boot: o codigo recompilado deve repetir o Flycast.

Usa dados locais do usuario (memoria do jogo, fora do git), apontados por
BS_SNAPSHOT_DIR:
    boot_entry.bss          estado na entrada 0x8C010000 (flycast_oracle.py snap)
    boot_flycast3k.tsv      trace de referencia (flycast_oracle.py trace --max-calls 3000)
    cp_entry.bss            entrada do jogo da sessao que gerou os checkpoints
    cp_<FUNC>_<N>.bss/.vram checkpoints (snap <FUNC> --hit N --no-mask-irq --vram --entry):
                            estado completo (registradores, FPU, RAM, VRAM) deve ser IGUAL
Sem BS_SNAPSHOT_DIR, sai com 77 (SKIP no CTest).

    python boot_regression.py <bs-diff.exe> <flycast_oracle.py> <aceitas.tsv>
"""
import glob
import os
import re
import subprocess
import sys
import tempfile


def run(cmd, timeout=600):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    print(r.stdout.strip())
    return r


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
    failed = 0

    calls = sum(1 for _ in open(ref))
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "recomp.tsv")
        r = run([bs_diff, "trace", "0x8C010000", entry, out, str(calls)])
        if r.returncode != 0:
            print(r.stderr[-2000:])
            return 1
        failed |= subprocess.run([sys.executable, "-I", oracle, "first-diff", ref, out,
                                  "--accept", accepted]).returncode != 0

    cp_entry = os.path.join(snap, "cp_entry.bss")
    cps = sorted(glob.glob(os.path.join(snap, "cp_*_*.bss")))
    if cps and not os.path.isfile(cp_entry):
        print(f"falta {cp_entry} (entrada da mesma sessao dos checkpoints)")
        return 1
    for cp in cps:
        m = re.fullmatch(r"cp_([0-9A-Fa-f]{8})_(\d+)\.bss", os.path.basename(cp))
        if not m:
            continue
        print(f"checkpoint {m.group(1)} entrada {m.group(2)}:")
        r = run([bs_diff, "until", "0x8C010000", cp_entry, cp, "0x" + m.group(1), m.group(2)])
        failed |= r.returncode != 0

    # Marco de integracao (GD-ROM, store queues, TA, DMA do canal 2, interrupcoes e
    # threads): com o disco e a flash do modo diferencial (a do Flycast), o boot chega ao
    # 1o STARTRENDER real, com mais de uma thread do guest. Nao e equivalencia: so marco.
    if os.environ.get("BS_DISC_IMAGE") and os.environ.get("BS_FLASH_IMAGE"):
        env = dict(os.environ, BS_STOP_ON_STARTRENDER="1", BS_DIFF_WATCHDOG="600")
        r = subprocess.run([bs_diff, "trace", "0x8C010000", cp_entry if os.path.isfile(cp_entry) else entry,
                            os.devnull, "200000000"], capture_output=True, text=True, timeout=900, env=env)
        out = r.stdout
        m = re.search(r"threads do guest: (\d+)", out)
        ok = "parada pedida: STARTRENDER" in out and m and int(m.group(1)) >= 2
        print("marco STARTRENDER:", "ok" if ok else "FALHOU")
        if not ok:
            print(out[-1500:], r.stderr[-1500:])
        failed |= not ok
    else:
        print("marco STARTRENDER: ignorado (BS_DISC_IMAGE/BS_FLASH_IMAGE)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
