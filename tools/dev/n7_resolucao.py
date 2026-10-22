"""N7c: qualidade da resolucao interna (docs/N7-PLANO.md secao 19).

Compara as capturas de duas execucoes headless da mesma rota (--dump-every N --capture <dir>, mesmos VBlanks):
  - <original>: --resolucao-interna 480 (o modo fiel; vNNNNNN.bmp)
  - <escala>:   --resolucao-interna 1080 (vNNNNNN.bmp = a imagem reduzida que foi para a VRAM; vNNNNNN_hd.bmp = a imagem
                na resolucao interna, as linhas exibidas)

Por quadro:
  - reduzida x original (PSNR): a imagem maior, reduzida, continua sendo o mesmo quadro (so a cobertura parcial das
    bordas muda);
  - detalhe acima da resolucao original: o que sobra de uma imagem depois de reduzi-la ao tamanho original (media) e
    amplia-la de volta (bilinear), comparado com o mesmo numero para a original simplesmente ampliada. Ampliar o
    framebuffer nao cria esse detalhe (razao ~1); a geometria desenhada no alvo maior cria (bordas finas, degraus de
    1 pixel do alvo): razao > 1;
  - diferenca da imagem maior para a original ampliada (PSNR): baixo = so ampliou; a imagem tem de ser outra.

Uso: python tools/dev/n7_resolucao.py <dir_original> <dir_escala> [--min-detalhe 1.25] [--min-psnr-reduzida 25]
Sai com 1 se algum criterio falhar.
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)


def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def to_img(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def fine_detail(a, w, h):
    """energia do que a imagem tem acima da resolucao w x h (reduz pela media, amplia bilinear, subtrai)"""
    img = to_img(a)
    back = img.resize((w, h), Image.BOX).resize((a.shape[1], a.shape[0]), Image.BILINEAR)
    return float(np.mean(np.abs(a - np.asarray(back, dtype=np.float64))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("escala")
    ap.add_argument("--min-detalhe", type=float, default=1.25)
    ap.add_argument("--min-psnr-reduzida", type=float, default=25.0)
    a = ap.parse_args()
    orig, hi = pathlib.Path(a.original), pathlib.Path(a.escala)
    rows, fail = [], 0
    for hd in sorted(hi.glob("v*_hd.bmp")):
        name = hd.name.replace("_hd", "")
        o, d = orig / name, hi / name
        if not o.exists() or not d.exists():
            continue
        O, D, H = load(o), load(d), load(hd)
        if O.shape != D.shape:
            print(f"{name}: tamanhos diferentes {O.shape} x {D.shape}")
            fail += 1
            continue
        up = np.asarray(Image.open(o).convert("RGB").resize((H.shape[1], H.shape[0]), Image.BILINEAR), dtype=np.float64)
        p_red = psnr(O, D)
        if O.std() < 2:  # quadro sem conteudo (preto, fade): nada a medir
            rows.append((name, H.shape[1], H.shape[0], psnr(O, D), 0.0, psnr(up, H), True))
            continue
        det = fine_detail(H, O.shape[1], O.shape[0]) / max(fine_detail(up, O.shape[1], O.shape[0]), 1e-9)
        p_up = psnr(up, H)
        ok = p_red >= a.min_psnr_reduzida and det >= a.min_detalhe
        fail += not ok
        rows.append((name, H.shape[1], H.shape[0], p_red, det, p_up, ok))
    if not rows:
        print("nenhum quadro com _hd.bmp em comum")
        return 1
    print("quadro          alvo        reduzida x original  detalhe (x ampliada)  maior x original ampliada")
    for n, w, h, pr, de, pu, ok in rows:
        print(f"{n:15} {w}x{h:<6} {pr:8.1f} dB            {de:6.2f}                {pu:6.1f} dB   {'ok' if ok else 'FALHOU'}")
    print(f"{len(rows)} quadros, {fail} falha(s)")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
