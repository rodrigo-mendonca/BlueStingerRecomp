"""Matriz de validacao dos filmes do N5 (docs/N5-PLANO.md). Reproduzivel e sem conteudo do jogo: so
contagens e resultados. Fontes:
  - bs-video-probe <BSDATA>                         quadros, fps, amostras ADX, decodificacao
  - bs-video-probe --converter <dir> <BSDATA>       (opcional, --conversao) PSNR/SSIM, atraso e SNR do AAC
  - analysis/n5/o1/*.tsv (oraculo O1)                ctl observado, estados e retorno por ID
  - mods: suporte por ID/nome e o OPENING (ausente no disco US) pelos testes video_* da bs_video_tests

    python -I tools/dev/n5_matriz.py --probe <bs-video-probe.exe> --bsdata <BSDATA> --saida <matriz.md>
           [--o1 analysis/n5/o1] [--conversao <pasta de saida dos mp4>]
"""
import argparse
import glob
import os
import re
import subprocess

NOMES = ["OPENING.SFD", "NEWGAME.SFD", "E017.SFD", "E001.SFD", "E107.SFD", "E179.SFD", "E180.SFD", "E185A.SFD",
         "E185B.SFD", "E193.SFD", "E195.SFD", "E200A.SFD", "E200B.SFD", "ENDING.SFD", "E016.SFD", "E134.SFD",
         "ENDING2.SFD"]
FLAG_TABELA = {0: 1}  # bytes da tabela 8C157688 (+4): so o ID 0 tem flag; os demais 0

PROBE = re.compile(r"^(\S+)\s+(\d+)x(\d+) ([\d.]+) fps\s+(\d+) quadros =\s+([\d.]+) s \| ADX (\S+) (\d+) Hz (\d+) ch\s+(\d+) amostras =\s+([\d.]+) s")
CONV = re.compile(r"^(\S+)\s+(\d+) quadros \(mp4\s+(\d+)\) PSNR min\s+([\d.]+) med\s+([\d.]+) <40dB\s+(\d+) SSIM min ([\d.]+) med ([\d.]+) \| audio atraso (-?\d+) amostras SNR\s+([\d.]+) dB")


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace").stdout.splitlines()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--probe", required=True)
    p.add_argument("--bsdata", required=True)
    p.add_argument("--saida", required=True)
    p.add_argument("--o1", default=os.path.join("analysis", "n5", "o1"))
    p.add_argument("--conversao", default="")
    a = p.parse_args()

    info = {}
    for l in run([a.probe, a.bsdata]):
        m = PROBE.match(l)
        if m:
            info[m[1]] = m
    conv = {}
    if a.conversao:
        for l in run([a.probe, "--converter", a.conversao, a.bsdata]):
            m = CONV.match(l)
            if m:
                conv[m[1]] = m
    o1 = {}  # id -> (ctl, estados, retorno)
    for f in glob.glob(os.path.join(a.o1, "*.tsv")):
        cur = None
        for l in open(f, encoding="utf-8"):
            c = l.rstrip("\n").split("\t")
            if len(c) < 4:
                continue
            if c[2] == "entrada":
                mid = int(re.search(r"id=(\d+)", c[3])[1])
                cur = [re.search(r"ctl=(\w+)", c[3])[1], [], ""]
                o1[mid] = cur
            elif c[2] == "estado" and cur is not None:
                cur[1].append(str(int(c[3], 16)))
            elif c[2] == "retorno" and cur is not None:
                cur[2] = str(int(re.search(r"r0=(\w+)", c[3])[1], 16))
                cur = None

    linhas = ["| ID | SFD | no disco US | quadros | fps | duracao (s) | amostras ADX | ADX (s) | decodificacao | "
              "flag da tabela | ctl (O1) | estados (O1) | retorno (O1) | cor | PSNR med/min | SSIM med/min | "
              "atraso AAC | SNR audio | reproducao nativa | mod |",
              "|" + "---|" * 20]
    for i, n in enumerate(NOMES):
        m = info.get(n)
        presente = "sim" if m else "**nao**"
        q = m[5] if m else "-"
        fps = m[4] if m else "-"
        dur = m[6] if m else "-"
        amo = m[10] if m else "-"
        adxs = m[11] if m else "-"
        dec = ("ok" if m[7] == "ok" else m[7]) if m else "-"
        c = conv.get(n)
        psnr = f"{c[5]} / {c[4]}" if c else "pendente"
        ssim = f"{c[8]} / {c[7]}" if c else "pendente"
        atraso = c[9] if c else "pendente"
        snr = c[10] if c else "pendente"
        ob = o1.get(i)
        linhas.append(f"| {i} | {n} | {presente} | {q} | {fps} | {dur} | {amo} | {adxs} | {dec} | {FLAG_TABELA.get(i, 0)} | "
                      f"{ob[0] if ob else 'nao observado'} | {'-'.join(ob[1]) if ob else '-'} | {ob[2] if ob else '-'} | "
                      f"BT.601 TV; base/offset pendente O1b | {psnr} | {ssim} | {atraso} | {snr} | pendente (integracao) | "
                      f"{'suportado (fornece o ausente)' if not m else 'suportado'} |")
    open(a.saida, "w", encoding="utf-8").write(
        "# Matriz de validacao dos filmes (N5)\n\nGerada por tools/dev/n5_matriz.py; sem conteudo do jogo.\n\n" + "\n".join(linhas) + "\n")
    print(f"{len(NOMES)} filmes -> {a.saida} ({len(info)} no disco, {len(conv)} convertidos, {len(o1)} observados no O1)")


if __name__ == "__main__":
    main()
