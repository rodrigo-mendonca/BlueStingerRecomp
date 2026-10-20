"""O1b do N5: matematica de cor do filme no jogo original (Flycast) contra o quadro decodificado.

Le as capturas do framebuffer gravadas pelo n5_oraculo.py --capturas (RGB555, 640x476 entrelacado),
decodifica o MP4 do Builder (ffmpeg, so referencia de desenvolvimento) e, para cada captura, acha o
quadro do filme mais parecido e ajusta por canal fb = k * filme + c (minimos quadrados) na area
ocupada pelo filme. Com a cor base e o offset registrados no TSV do oraculo, mostra a relacao.

    python -I tools/dev/n5_cor.py --mp4 <filme.mp4> --capturas <dir> --prefixo filme1 [--quadros 700]
"""
import argparse
import glob
import os
import subprocess

import numpy as np


def fb_rgb(path):
    raw = np.frombuffer(open(path, "rb").read(), dtype="<u2")
    h = raw.size // 640
    px = raw[: h * 640].reshape(h, 640)
    r = ((px >> 10) & 31).astype(np.float64) * 255 / 31
    g = ((px >> 5) & 31).astype(np.float64) * 255 / 31
    b = (px & 31).astype(np.float64) * 255 / 31
    return np.stack([r, g, b], axis=-1)  # h x 640 x 3


def decode(mp4, n):
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", mp4, "-frames:v", str(n), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                       capture_output=True)
    quadro = 320 * 240 * 3
    if r.returncode != 0 or not r.stdout or len(r.stdout) % quadro:
        raise SystemExit(f"ffmpeg falhou ({r.returncode}): {r.stderr.decode(errors='replace').strip()[:300]}")
    f = np.frombuffer(r.stdout, dtype=np.uint8)
    return f.reshape(-1, 240, 320, 3).astype(np.float64)


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--mp4", required=True)
    a.add_argument("--capturas", required=True)
    a.add_argument("--prefixo", default="filme1")
    a.add_argument("--quadros", type=int, default=700)
    a.add_argument("--modelo", default="", help="tv,base,offset (ex.: 1,1,1) para medir o modelo contra as capturas")
    a.add_argument("--base", type=lambda x: int(x, 0), default=0xC0)
    a.add_argument("--offset", type=lambda x: int(x, 0), default=0)
    o = a.parse_args()
    frames = decode(o.mp4, o.quadros)
    print(f"{len(frames)} quadros decodificados de {os.path.basename(o.mp4)}")
    for cap in sorted(glob.glob(os.path.join(o.capturas, o.prefixo + "_it*.fb"))):
        fb = fb_rgb(cap)
        # o filme 320x240 ocupa a tela inteira (640x480) com 2x2; reduz o fb para 320x238 pela media
        h = fb.shape[0] // 2 * 2
        small = fb[:h].reshape(h // 2, 2, 320, 2, 3).mean(axis=(1, 3))  # 238 x 320 x 3
        rows = small.shape[0]
        best, bi = None, -1
        for i, f in enumerate(frames):
            ref = f[:rows]
            # correlacao da luminancia (invariante a ganho e offset)
            x = ref.mean(axis=2).ravel()
            y = small.mean(axis=2).ravel()
            if x.std() < 1 or y.std() < 1:
                continue
            c = np.corrcoef(x, y)[0, 1]
            if best is None or c > best:
                best, bi = c, i
        name = os.path.basename(cap)
        if bi < 0:
            print(f"{name}: tela sem detalhe (preta ou uniforme): media {small.mean():.1f}")
            continue
        ref = frames[bi][:rows]
        fits = []
        for ch in range(3):
            x = ref[..., ch].ravel()
            y = small[..., ch].ravel()
            k, c = np.polyfit(x, y, 1)
            res = y - (k * x + c)
            fits.append((k, c, np.sqrt((res ** 2).mean())))
        ks = " ".join(f"k={k:.3f} c={c:+.1f} (resid {r:.1f})" for k, c, r in fits)
        print(f"{name}: quadro {bi} (correlacao {best:.4f}); {ks}")
        if o.modelo:
            tv, mb, ao = (int(v) for v in o.modelo.split(","))
            pred = 16 + ref * 219 / 255 if tv else ref.copy()
            if mb:
                pred = pred * (o.base / 255)
            if ao:
                pred = pred + o.offset
            pred = np.clip(np.round(pred), 0, 255)
            ident = np.clip(ref, 0, 255)
            def psnr(x):
                mse = ((x - small) ** 2).mean()
                return 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
            print(f"    modelo {o.modelo} (base {o.base:#04x}, offset {o.offset}): PSNR {psnr(pred):.2f} dB; identidade: {psnr(ident):.2f} dB")


if __name__ == "__main__":
    main()
