"""Gera app/res/bluestinger.ico: o icone do executavel do jogo (Blue Stinger Native PC).

Desenho ORIGINAL do projeto, so geometria (sem logotipo, arte, fonte ou imagem do jogo): um ferrao azul estilizado
em diagonal sobre um quadrado escuro arredondado, com um ponto ambar. Tamanhos 16, 24, 32, 48, 64, 128 e 256 (o
Windows escolhe pelo uso). Cada tamanho e desenhado a 8x e reduzido (bordas suaves).

    python tools/dev/gerar_icone.py [saida.ico]
    python tools/dev/gerar_icone.py --capa <imagem da capa> [--recorte x,y,lado]   (icone-local/, so uso local)

Com --capa, o icone vem da imagem da capa do proprio usuario e vai para icone-local/bluestinger.ico (fora do git): os
builds locais (app/CMakeLists.txt) o usam no lugar do original; make_builder.ps1 -IconeLocal o leva ao kit local.
"""
import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAMANHOS = [16, 24, 32, 48, 64, 128, 256]


def desenho(px):
    s = px * 8  # supersampling
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    m = s * 0.04
    d.rounded_rectangle([m, m, s - m, s - m], radius=s * 0.2, fill=(12, 20, 38, 255), outline=(40, 90, 170, 255),
                        width=max(1, int(s * 0.025)))
    # ferrao: uma gota afilada na diagonal (base em cima a esquerda, ponta embaixo a direita), com faixas e um brilho
    base = (s * 0.27, s * 0.25)
    ponta = (s * 0.80, s * 0.80)
    ang = math.atan2(ponta[1] - base[1], ponta[0] - base[0])
    larg = s * 0.17
    nx, ny = -math.sin(ang), math.cos(ang)

    def largura(t):  # gota: base arredondada, mais larga perto de 1/3, afinando ate a ponta
        return larg * 1.75 * math.sqrt(t) * (1 - t) ** 1.15

    def ponto(t, lado):
        w = largura(t) * lado
        return base[0] + (ponta[0] - base[0]) * t + nx * w, base[1] + (ponta[1] - base[1]) * t + ny * w

    pontos = [ponto(i / 60, 1) for i in range(61)] + [ponto(i / 60, -1) for i in range(60, -1, -1)]
    d.polygon(pontos, fill=(40, 130, 230, 255))
    # duas faixas escuras atravessando o corpo e um brilho no lado de cima
    for k in (0.2, 0.36):
        d.line([ponto(k, 1.1), ponto(k, -1.1)], fill=(12, 20, 38, 255), width=max(1, int(s * 0.035)))
    d.line([ponto(i / 40 * 0.9 + 0.05, -0.55) for i in range(41)], fill=(150, 210, 255, 255), width=max(1, int(s * 0.025)))
    # ponto ambar
    r = s * 0.075
    c = (s * 0.72, s * 0.28)
    d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=(255, 176, 40, 255))
    return im.resize((px, px), Image.LANCZOS)


def da_capa(caminho, recorte):
    """Os tamanhos do icone a partir da imagem da capa do proprio usuario: um quadrado recortado (x,y,lado em pixels da
    imagem; sem recorte, o maior quadrado centralizado) e reduzido com LANCZOS."""
    im = Image.open(caminho).convert("RGBA")
    if recorte:
        x, y, lado = (int(v) for v in recorte.split(","))
    else:
        lado = min(im.size)
        x, y = (im.width - lado) // 2, (im.height - lado) // 2
    if x < 0 or y < 0 or lado <= 0 or x + lado > im.width or y + lado > im.height:
        raise SystemExit("recorte %s fora da imagem %dx%d" % (recorte, im.width, im.height))
    # capa escaneada: o grao do JPEG vira ruido nos tamanhos pequenos. Tira o grao na resolucao original (mediana 3x3 +
    # desfoque leve), reduz em passos (no maximo 2x por vez) e devolve a nitidez por tamanho (mais forte nos pequenos),
    # com um pouco mais de contraste e cor para o icone nao ficar lavado.
    q = im.crop((x, y, x + lado, y + lado))
    rgb, alfa = q.convert("RGB").filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.GaussianBlur(0.6)), q.getchannel("A")
    q = Image.merge("RGBA", (*rgb.split(), alfa))
    q = ImageEnhance.Contrast(ImageEnhance.Color(q).enhance(1.12)).enhance(1.08)
    saida = []
    for p in TAMANHOS:
        t = q
        while t.width > 2 * p:
            t = t.resize((t.width // 2, t.height // 2), Image.LANCZOS)
        t = t.resize((p, p), Image.LANCZOS)
        forca = 160 if p <= 32 else 110 if p <= 64 else 70
        saida.append(t.filter(ImageFilter.UnsharpMask(radius=0.8 if p <= 32 else 1.2, percent=forca, threshold=2)))
    return saida


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("saida", nargs="?")
    ap.add_argument("--capa", help="imagem da capa do PROPRIO usuario; a saida padrao vira icone-local/bluestinger.ico "
                                   "(fora do git: arte do jogo, so uso local)")
    ap.add_argument("--recorte", help="x,y,lado do quadrado na imagem da capa (padrao: o maior quadrado centralizado)")
    a = ap.parse_args()
    if a.capa:
        saida = a.saida or os.path.join(REPO, "icone-local", "bluestinger.ico")
        imgs = da_capa(a.capa, a.recorte)
    else:
        saida = a.saida or os.path.join(REPO, "app", "res", "bluestinger.ico")
        imgs = [desenho(p) for p in TAMANHOS]
    os.makedirs(os.path.dirname(os.path.abspath(saida)), exist_ok=True)
    imgs[-1].save(saida, format="ICO", sizes=[(p, p) for p in TAMANHOS], append_images=imgs[:-1])
    imgs[-1].save(os.path.join(os.environ.get("TEMP", "."), "bluestinger_icone_256.png"))  # so para ver (fora do repositorio)
    print(saida)


if __name__ == "__main__":
    main()
