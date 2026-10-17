"""Cliente do gameplay assistido (app/assist.h), so desenvolvimento.

  python tools/dev/assistido.py iniciar <pasta> [--tempo nativo|experimental] [--vblanks N]
                                        [--exe <bluestinger.exe>] [--data <pasta data>]
  python tools/dev/assistido.py cmd <pasta> "<comando>" ["<comando>" ...]
  python tools/dev/assistido.py estado <pasta>
  python tools/dev/assistido.py pos <pasta>      (posicao do jogador, x y z)
  python tools/dev/assistido.py floats <pasta> <endereco> <n>   (n floats da RAM)
  python tools/dev/assistido.py mosaico <saida.png> <quadro.png> [...]   (grade reduzida a 1/2)

`iniciar` abre o jogo headless, destacado deste processo, com BS_ASSISTIDO=<pasta>, relogio fixo e
log de arquivos (<pasta>/arquivos.tsv) e de quadros (<pasta>/quadros.tsv); espera a primeira pausa.
`cmd` acrescenta os comandos em <pasta>/comandos.txt, espera o jogo atender todos e pausar, e
imprime as respostas novas e o estado (VBlank, passo de jogo, leituras do controle, botoes).
A sessao (roteiro "indice passo") fica em <pasta>/sessao.txt.
"""
import argparse
import os
import struct
import zlib
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def state(d):
    s = read(os.path.join(d, "estado.txt")).strip().split("\t")
    if len(s) < 6 or s[0] != "pronto":
        return None
    return {"atendidos": int(s[1]), "vblank": int(s[2]), "passo": int(s[3]), "leituras": int(s[4]), "pad": s[5]}


def alive(d):
    out = read(os.path.join(d, "saida.txt"))
    return "VBlanks:" not in out and "erro:" not in out


def wait(d, n, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        st = state(d)
        if st and st["atendidos"] >= n:
            return st
        if not alive(d):
            sys.exit("o jogo terminou:\n" + read(os.path.join(d, "saida.txt"))[-2000:])
        time.sleep(0.05)
    sys.exit("tempo esgotado esperando a pausa")


def count_lines(path):
    return read(path).count("\n")


def png_read(path):
    """PNG RGB de 8 bits (os quadros gravados pelo jogo: filtro 0)."""
    data = open(path, "rb").read()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    return w, h, [raw[y * (1 + 3 * w) + 1:(y + 1) * (1 + 3 * w)] for y in range(h)]


def png_write(path, w, h, rows):
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def mosaic(out, files, cols=3):
    imgs = []
    for f in files:
        w, h, rows = png_read(f)
        imgs.append([bytes(b for x in range(0, w, 2) for b in r[3 * x:3 * x + 3]) for r in rows[::2]])
    tw, th = len(imgs[0][0]) // 3, len(imgs[0])
    gw, gh = min(cols, len(imgs)), (len(imgs) + cols - 1) // cols
    rows = []
    for gy in range(gh):
        for y in range(th):
            line = bytearray()
            for gx in range(gw):
                k = gy * cols + gx
                line += imgs[k][y] if k < len(imgs) and y < len(imgs[k]) else bytes(3 * tw)
            rows.append(line)
    png_write(out, gw * tw, gh * th, rows)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="op", required=True)
    a = sub.add_parser("iniciar")
    a.add_argument("pasta")
    a.add_argument("--tempo", default="nativo")
    a.add_argument("--vblanks", type=int, default=10_000_000)
    a.add_argument("--exe", default=os.path.join(REPO, "build", "msvc-release", "app", "bluestinger.exe"))
    a.add_argument("--data", default=os.path.join(REPO, "dist", "BlueStinger-Preview", "data"))
    c = sub.add_parser("cmd")
    c.add_argument("pasta")
    c.add_argument("comandos", nargs="+")
    c.add_argument("--limite", type=float, default=1800)
    e = sub.add_parser("estado")
    e.add_argument("pasta")
    q = sub.add_parser("pos")
    q.add_argument("pasta")
    fl = sub.add_parser("floats")
    fl.add_argument("pasta")
    fl.add_argument("endereco")
    fl.add_argument("n", type=int)
    m = sub.add_parser("mosaico")
    m.add_argument("saida")
    m.add_argument("quadros", nargs="+")
    args = ap.parse_args()
    if args.op == "mosaico":
        mosaic(args.saida, args.quadros)
        return
    d = os.path.abspath(args.pasta)

    if args.op == "iniciar":
        os.makedirs(d, exist_ok=True)
        st = state(d)
        if st and alive(d) and os.path.exists(os.path.join(d, "saida.txt")):
            sys.exit("ja ha uma sessao nesta pasta (mande 'fim' antes)")
        env = dict(os.environ, BS_ASSISTIDO=d, BS_RELOGIO_FIXO="1700000000",
                   BS_FILE_LOG=os.path.join(d, "arquivos.tsv"), BS_FRAME_LOG=os.path.join(d, "quadros.tsv"))
        env.pop("BS_INPUT_SCRIPT", None)
        for f in ("estado.txt", "saida.txt"):
            try:
                os.remove(os.path.join(d, f))
            except OSError:
                pass
        out = open(os.path.join(d, "saida.txt"), "w")
        flags = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
        p = subprocess.Popen([args.exe, "--data", args.data, "--tempo", args.tempo, "--headless", str(args.vblanks)],
                             env=env, stdout=out, stderr=subprocess.STDOUT, creationflags=flags, close_fds=True)
        st = wait(d, 0, 120)
        print(f"pid {p.pid}; pausado: {st}")
        return

    if args.op == "estado":
        print(state(d), "vivo" if alive(d) else "terminado")
        return

    if args.op == "pos":
        args.comandos, args.limite = ["ram 0x8C24D668 12"], 60
    if args.op == "floats":
        args.comandos, args.limite = [f"ram {args.endereco} {4 * args.n}"], 60
    cmds = os.path.join(d, "comandos.txt")
    resp = os.path.join(d, "respostas.txt")
    seen = len(read(resp))
    with open(cmds, "a", encoding="utf-8", newline="\n") as f:
        for cmd in args.comandos:
            f.write(cmd.strip() + "\n")
    n = count_lines(cmds)
    if any(x.strip() == "fim" for x in args.comandos):
        time.sleep(1)
        print(read(resp)[seen:], end="")
        return
    st = wait(d, n, args.limite)
    if args.op in ("pos", "floats"):
        b = b"".join(bytes.fromhex(l.split(":", 1)[1].strip().replace(" ", ""))
                     for l in read(resp)[seen:].splitlines() if ":" in l)
        v = struct.unpack("<%df" % (len(b) // 4), b)
        print("pos x %.1f y %.1f z %.1f" % v if args.op == "pos" else " ".join("%.2f" % x for x in v))
    else:
        print(read(resp)[seen:], end="")
    print(f"estado: VBlank {st['vblank']}, passo {st['passo']}, leituras {st['leituras']}, pad {st['pad']}")


if __name__ == "__main__":
    main()
