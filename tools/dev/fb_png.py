#!/usr/bin/env python3
"""Converte o framebuffer gravado pelo PVR numa VRAM capturada em PNG e, com dois
arquivos, compara pixel a pixel.

  fb_png.py <estado.vram> [-o saida.png]
  fb_png.py <a.vram> <b.vram> [-o diff.png]

Cada <x>.vram (8 MB, caminho de 32 bits) precisa de <x>.pvr ao lado (banco do PVR,
2048 palavras desde 0x005F8000; `flycast_oracle.py snap --pvr` ou BS_DIFF_SAVE do
bs-diff). Le o buffer de escrita do render: FB_W_SOF1, FB_W_LINESTRIDE, FB_W_CTRL e
FB_X/Y_CLIP; com --read, o buffer exibido (FB_R_SOF1, FB_R_CTRL, FB_R_SIZE). So a biblioteca padrao.
"""
import argparse
import os
import struct
import sys
import zlib


def reg(bank, off):
    return bank[off // 4]


def load(path):
    vram = open(path, "rb").read()
    pvr = open(os.path.splitext(path)[0] + ".pvr", "rb").read()
    return vram, struct.unpack("<2048I", pvr)


def framebuffer(vram, bank, read=False, sof_override=None):
    if read:  # buffer exibido: FB_R_SOF1, FB_R_CTRL[3:2] (0 0555, 1 565, 2 888, 3 0888), FB_R_SIZE
        fmt = [0, 1, 4, 5][(reg(bank, 0x044) >> 2) & 3]
        bpp = [2, 2, 2, 2, 3, 4, 4, 4][fmt]
        sof = reg(bank, 0x050) & 0x00FFFFFC
        size = reg(bank, 0x05C)
        xw, h, mod = (size & 0x3FF) + 1, ((size >> 10) & 0x3FF) + 1, (size >> 20) & 0x3FF
        w, stride = xw * 4 // bpp, (xw + mod - 1) * 4
    else:  # buffer de escrita do render
        ctrl = reg(bank, 0x048)
        sof = reg(bank, 0x060) & 0x00FFFFFC
        stride = (reg(bank, 0x04C) & 0x1FF) * 8
        xc, yc = reg(bank, 0x068), reg(bank, 0x06C)
        w = min(((xc >> 16) & 0x7FF) + 1, 2048)
        h = min(((yc >> 16) & 0x3FF) + 1, 1024)
        fmt = ctrl & 7
        bpp = [2, 2, 2, 2, 3, 4, 4, 4][fmt]
    if sof_override is not None:
        sof = sof_override
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            a = (sof + y * stride + x * bpp) & 0x7FFFFF
            if bpp == 2:
                v = vram[a] | (vram[a + 1] << 8)
                if fmt in (0, 3):
                    r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
                    r, g, b = r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2
                elif fmt == 1:
                    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                    r, g, b = r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2
                else:
                    r, g, b = ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17
            else:
                b, g, r = vram[a], vram[a + 1], vram[a + 2]
            row += bytes((r, g, b))
        rows.append(bytes(row))
    return w, h, rows, (sof, stride, fmt)


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vram", nargs="+")
    ap.add_argument("-o", "--out")
    ap.add_argument("--sof", type=lambda x: int(x, 0), help="endereco do buffer (ex.: FB_W_SOF2 do quadro anterior)")
    ap.add_argument("--read", action="store_true", help="buffer exibido (FB_R_*) em vez do de escrita (FB_W_*)")
    a = ap.parse_args()
    fbs = []
    for p in a.vram[:2]:
        w, h, rows, info = framebuffer(*load(p), a.read, a.sof)
        print(f"{p}: {w}x{h} endereco={info[0]:06X} stride={info[1]} formato={info[2]}")
        fbs.append((w, h, rows, info[2]))
    if len(fbs) == 1:
        write_png(a.out or os.path.splitext(a.vram[0])[0] + ".png", *fbs[0][:3])
        return 0
    (w, h, ra, fmt), (w2, h2, rb, _) = fbs
    # um passo de quantizacao em 8 bits por formato: 5 bits ~8 (565/0555/1555), 4 bits 17
    # (4444), 8 bits 1 (888/0888/8888); margem de 2 para o arredondamento da expansao
    step = {0: 10, 1: 10, 2: 19, 3: 10}.get(fmt, 1)
    if (w, h) != (w2, h2):
        print("tamanhos diferentes")
        return 1
    # Classes: "1 passo" = no maximo um passo de quantizacao do formato por canal
    # (diferenca compativel com dither ou arredondamento); "grande"
    # = o resto. Mapa: igual = imagem escurecida, 1 passo = azul, grande = vermelho.
    diff, small, out, maxd = 0, 0, [], 0
    box = [w, h, -1, -1]
    for y in range(h):
        row = bytearray()
        for x in range(w):
            pa, pb = ra[y][3 * x:3 * x + 3], rb[y][3 * x:3 * x + 3]
            d = max(abs(pa[i] - pb[i]) for i in range(3))
            maxd = max(maxd, d)
            if not d:
                row += bytes(c // 4 for c in pa)
                continue
            diff += 1
            if d <= step:
                small += 1
                row += bytes((0, 0, 160))
            else:
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
                row += bytes((255, 40, 40))
        out.append(bytes(row))
    big = diff - small
    print(f"pixels diferentes: {diff} de {w * h} ({100.0 * diff / (w * h):.2f}%), maior diferenca por canal {maxd}")
    print(f"  ate 1 passo de quantizacao: {small} ({100.0 * small / (w * h):.2f}%)")
    print(f"  maiores: {big} ({100.0 * big / (w * h):.2f}%)" + (f", na regiao {box}" if big else ""))
    if a.out:
        write_png(a.out, w, h, out)
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
