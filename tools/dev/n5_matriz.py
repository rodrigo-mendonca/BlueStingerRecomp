"""Matriz de validacao dos filmes do N5 (docs/N5-PLANO.md), reproduzivel e sem conteudo do jogo:
so contagens e medidas. Nao reconverte nada. Fontes:
  - bs-video-probe <BSDATA>                    SFD do disco: quadros, fps, amostras ADX, decodificacao
  - <data\\video>\\manifest.toml               medidas da conversao feitas pelo Builder (PSNR/SSIM por
                                               quadro, atraso do AAC compensado, SNR do audio, SHA-256)
  - bs-video-probe --tocar <data\\video> [mods] reproducao pelo MoviePlayback com o relogio logico:
                                               integridade (SHA-256), quadros, fins, sincronia, mods
  - analysis/n5/o1*/*.tsv (oraculo)           ctl, estados e retorno observados no Flycast

    python -I tools/dev/n5_matriz.py --probe <bs-video-probe.exe> --bsdata <BSDATA> --video <data\\video>
           [--mods <mods\\video>] [--o1 analysis/n5] --saida <matriz.md>
"""
import argparse
import glob
import os
import re
import subprocess
import tomllib

NOMES = ["OPENING.SFD", "NEWGAME.SFD", "E017.SFD", "E001.SFD", "E107.SFD", "E179.SFD", "E180.SFD", "E185A.SFD",
         "E185B.SFD", "E193.SFD", "E195.SFD", "E200A.SFD", "E200B.SFD", "ENDING.SFD", "E016.SFD", "E134.SFD",
         "ENDING2.SFD"]
PROBE = re.compile(r"^(\S+)\s+(\d+)x(\d+) ([\d.]+) fps\s+(\d+) quadros =\s+([\d.]+) s \| ADX (\S+) (\d+) Hz (\d+) ch\s+(\d+) amostras =\s+([\d.]+) s")
PSNR_OBJETIVO = 40.0  # D4: objetivo, nao bloqueio
SYNC_MS = 40          # diferenca entre os fins de video e audio aceitavel (pouco mais de um quadro)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--probe", required=True)
    p.add_argument("--bsdata", required=True)
    p.add_argument("--video", required=True)
    p.add_argument("--mods", default="")
    p.add_argument("--o1", default=os.path.join("analysis", "n5"))
    p.add_argument("--saida", required=True)
    a = p.parse_args()

    erros = []
    rc0, linhas0 = run([a.probe, a.bsdata])
    if rc0 != 0:
        erros.append(f"bs-video-probe <BSDATA> devolveu {rc0}")
    sfd = {}
    for l in linhas0:
        m = PROBE.match(l)
        if m:
            sfd[m[1]] = m
    man = {}
    mp = os.path.join(a.video, "manifest.toml")
    if os.path.exists(mp):
        with open(mp, "rb") as f:
            for e in tomllib.load(f).get("filme", []):
                man[int(e["id"])] = e
    rc, lines = run([a.probe, "--tocar", a.video] + ([a.mods] if a.mods else []))
    toc = {}
    for l in lines:
        c = l.split("\t")
        if len(c) == 13 and c[0].isdigit():
            toc[int(c[0])] = c
    if rc != 0:
        erros.append(f"bs-video-probe --tocar devolveu {rc}")
    faltam = [i for i in range(len(NOMES)) if i not in toc]
    if faltam:
        erros.append(f"--tocar sem linha para os IDs {faltam}")
    o1 = {}
    for f in glob.glob(os.path.join(a.o1, "**", "*.tsv"), recursive=True):
        cur = None
        for l in open(f, encoding="utf-8", errors="replace"):
            c = l.rstrip("\n").split("\t")
            if len(c) < 4:
                continue
            if c[2] == "entrada" and "id=" in c[3]:
                mid = int(re.search(r"id=(\d+)", c[3])[1])
                cur = [re.search(r"ctl=(\w+)", c[3])[1], [], ""]
                o1[mid] = cur
            elif c[2] == "estado" and cur is not None:
                v = str(int(c[3], 16))
                if not cur[1] or cur[1][-1] != v:
                    cur[1].append(v)
            elif c[2] == "retorno" and cur is not None:
                cur[2] = str(int(re.search(r"r0=(\w+)", c[3])[1], 16))
                cur = None

    head = ["ID", "original", "disco", "quadros SFD", "duração vídeo (s)", "duração áudio (s)", "origem", "SHA-256",
            "quadros tocados", "fim vídeo (ms)", "fim áudio (ms)", "sincronia (ms)", "atraso AAC compensado (amostras)",
            "PSNR méd/mín (dB)", "SSIM méd/mín", "SNR áudio (dB)", "reprodução", "ctl (O1)", "estados (O1)", "retorno (O1)",
            "EOF do áudio no MP4 (ms)"]
    rows, notas, falhas = [], [], 0
    for i, n in enumerate(NOMES):
        s = sfd.get(n)
        e = man.get(i, {})
        t = toc.get(i)
        o = o1.get(i)
        res = t[12] if t else "sem resultado"
        sync = t[8] if t else "-"
        if t and res == "ok" and sync not in ("-", "") and abs(int(sync)) > SYNC_MS:
            res = f"ok (sincronia {sync} ms)"
            notas.append(f"{n}: fins de vídeo e áudio diferem {sync} ms")
        if not res.startswith("ok") and res != "ausente":
            falhas += 1
        psnr = f"{e['psnr_med']:.2f} / {e['psnr_min']:.2f}" if "psnr_med" in e else "-"
        if "psnr_min" in e and e["psnr_min"] < PSNR_OBJETIVO:
            notas.append(f"{n}: PSNR mínimo {e['psnr_min']:.2f} dB (objetivo de {PSNR_OBJETIVO:.0f} dB, D4: não bloqueia)")
        rows.append([str(i), n, "sim" if s else "**não**", s[5] if s else "-", s[6] if s else "-", s[11] if s else "-",
                     t[2] if t else "-", t[3] if t else "-", t[4] if t else "-", t[6] if t else "-", t[7] if t else "-",
                     sync, str(e.get("atraso_audio_amostras", "-")), psnr,
                     f"{e['ssim_med']:.4f} / {e['ssim_min']:.4f}" if "ssim_med" in e else "-",
                     f"{e['snr_audio']:.1f}" if "snr_audio" in e else "-", res,
                     o[0] if o else "não observado", "-".join(o[1]) if o and o[1] else "-", o[2] if o and o[2] else "-",
                     t[11] if t else "-"])
    out = ["# Matriz de validação dos filmes (N5)", "",
           "Gerada por `tools/dev/n5_matriz.py`, sem conteúdo do jogo e sem reconverter nada.",
           "As medidas de conversão (PSNR/SSIM por quadro, atraso e SNR do AAC) são as gravadas pelo Builder no manifesto.",
           "A reprodução é a do `MoviePlayback`, com o relógio lógico a 60 Hz e os parâmetros do jogo. Os fins de vídeo e áudio",
           "são os que governam o fim no jogo (filme do Builder: os da fonte, no manifesto); o instante do fim é conferido contra",
           "a regra (o mais tardio dos dois). O EOF do áudio no MP4 inclui o preenchimento do AAC (diagnóstico).", "",
           "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    out += ["", f"Reprodução: {sum(1 for r in rows if r[16].startswith('ok'))} ok, "
                f"{sum(1 for r in rows if r[16] == 'ausente')} ausentes, {falhas} com falha."]
    if notas:
        out += ["", "Observações:"] + [f"- {x}" for x in notas]
    if erros:
        out += ["", "**Erros da coleta:**"] + [f"- {x}" for x in erros]
    with open(a.saida, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"{len(NOMES)} filmes -> {a.saida}: {falhas} falhas de reprodução, {len(notas)} observações, {len(erros)} erros de coleta")
    return 1 if falhas or erros else 0


if __name__ == "__main__":
    raise SystemExit(main())
