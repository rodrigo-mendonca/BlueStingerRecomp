"""Navegacao do gameplay assistido ate um ponto (x, z) no plano do chao (so desenvolvimento).

  python tools/dev/assistido_ir.py <pasta> <x> <z> [--raio R] [--max N]

O controle do jogo e relativo a camera (medido: com angulo = atan2(z, x), UP anda na direcao da
camera, RIGHT a +90 graus, LEFT a -90, DOWN a 180; o personagem gira com inercia e a camera o
segue). A cada rodada le a posicao do jogador (0x8C24D668: x, y, z) e o vetor de visao da camera
(0x8C24CC84: x, y, z, logo depois da posicao dela em 0x8C24CC78), escolhe a direcao do controle
(das 8) mais proxima do alvo e anda alguns passos. Para a menos de R unidades. Os comandos vao
pela sessao (sessao.txt): o caminho fica gravado e se reproduz.
"""
import argparse
import math
import os
import subprocess
import sys

A = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assistido.py")


def cmd(d, *c):
    subprocess.run([sys.executable, A, "cmd", d, *c], capture_output=True, text=True, check=True)


def pos(d):
    out = subprocess.run([sys.executable, A, "pos", d], capture_output=True, text=True, check=True).stdout.split()
    return float(out[2]), float(out[6])


def floats(d, addr, n):
    out = subprocess.run([sys.executable, A, "floats", d, addr, str(n)], capture_output=True, text=True, check=True)
    return [float(v) for v in out.stdout.split()[:n]]


PADS = {0: "UP", 45: "UP+RIGHT", 90: "RIGHT", 135: "DOWN+RIGHT", 180: "DOWN", -135: "DOWN+LEFT", -90: "LEFT",
        -45: "UP+LEFT"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pasta")
    ap.add_argument("x", type=float)
    ap.add_argument("z", type=float)
    ap.add_argument("--raio", type=float, default=30)
    ap.add_argument("--max", type=int, default=40)
    a = ap.parse_args()
    d = a.pasta
    for i in range(a.max):
        p = floats(d, "0x8C24D668", 3)
        f = floats(d, "0x8C24CC84", 3)
        x, z = p[0], p[2]
        dist = math.hypot(a.x - x, a.z - z)
        if dist < a.raio:
            print(f"chegou: ({x:.0f}, {z:.0f}), distancia {dist:.0f}")
            return 0
        rel = (math.degrees(math.atan2(a.z - z, a.x - x)) - math.degrees(math.atan2(f[2], f[0])) + 540) % 360 - 180
        q = int(round(rel / 45.0)) * 45
        q = 180 if abs(q) == 180 else q
        steps = max(2, min(12, int(dist / 10)))
        cmd(d, f"pad {PADS[q]}", f"passos {steps}", "pad -", "passos 1")
        print(f"rodada {i}: ({x:.0f}, {z:.0f}) distancia {dist:.0f}, relativo {rel:+.0f} -> {PADS[q]} x{steps}")
    print("nao chegou")
    return 1


if __name__ == "__main__":
    sys.exit(main())
