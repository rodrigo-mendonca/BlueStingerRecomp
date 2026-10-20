"""Legendas EN (docs/LEGENDAS-EN-PLANO.md): previa para revisao humana, fora do jogo e fora do Git.

Para cada voz pedida, um MP4 (tela preta) com o audio da copia PCM e a legenda como faixa (mov_text) e o mesmo SRT ao
lado: basta abrir no VLC/mpv e conferir o texto e os tempos. Usa so o pacote gerado (gerar_mod.py) e o catalogo.

    python tools/dev/legendas/previa.py [--trabalho <pasta>] [--vozes e002a,e002b,...]
"""
import argparse
import os
import subprocess
import sys
import tomllib

TRABALHO = os.path.join(os.environ.get("LOCALAPPDATA", "."), "BlueStingerRecomp", "legendas-en")


def srt_tempo(t):
    ms = int(round(t * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabalho", default=TRABALHO)
    ap.add_argument("--vozes", default="e002a,e002b,e003,e004a,e004b,e005")
    a = ap.parse_args()
    falas = tomllib.load(open(os.path.join(a.trabalho, "mod", "legendas-en", "legendas", "falas.en-US.toml"), "rb"))["fala"]
    dest = os.path.join(a.trabalho, "previa")
    os.makedirs(dest, exist_ok=True)
    for v in [x.strip().lower() for x in a.vozes.split(",") if x.strip()]:
        ls = [f for f in falas if f["voz"] == "voz/" + v]
        wav = os.path.join(a.trabalho, "pcm", v.upper() + ".wav")
        if not ls or not os.path.exists(wav):
            print(f"{v}: sem legendas ou sem a copia PCM")
            continue
        srt = os.path.join(dest, v.upper() + ".en-US.srt")
        with open(srt, "w", encoding="utf-8", newline="\n") as o:
            for k, f in enumerate(ls, 1):
                o.write(f"{k}\n{srt_tempo(f['inicio'])} --> {srt_tempo(f['fim'])}\n{f['texto']}\n\n")
        mp4 = os.path.join(dest, v.upper() + ".mp4")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=black:s=640x360:r=30", "-i", wav, "-i", srt,
                        "-map", "0:v", "-map", "1:a", "-map", "2:s", "-c:v", "libx264", "-preset", "veryfast", "-tune", "stillimage",
                        "-c:a", "aac", "-b:a", "128k", "-c:s", "mov_text", "-metadata:s:s:0", "language=eng", "-shortest", mp4],
                       check=True)
        print(f"{v}: {len(ls)} linhas -> {mp4}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
