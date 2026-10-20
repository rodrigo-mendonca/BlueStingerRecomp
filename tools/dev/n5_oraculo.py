"""Oraculo O1 do N5 (docs/N5-PLANO.md): a funcao de video do jogo original no Flycast.

Registra, por passo de jogo (passada do servidor pd, como o bu-trace):
  entrada  8C08B544 r4 = ID, r5 = controle (exts.b) e o nome da tabela 8C157688
  estado   cada valor NOVO devolvido por mwSfdGetStat (8C1582C0), com o passo
  sofdec   create (8C157FC4), start (8C15814A), stop (8C158250): passo
  retorno  r0 de 8C08B544 e os passos desde a entrada
Entrada injetada pelo roteiro "indice passo" (com @ancora/@pulsos), a mesma do bu-trace.

    python tools/dev/n5_oraculo.py <saida.tsv> --roteiro <r> [--passos N] [--game <cdi>]
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flycast_oracle as fo  # noqa: E402

MOVIE, GETSTAT, CREATE, START, STOP = 0x8C08B544, 0x8C1582C0, 0x8C157FC4, 0x8C15814A, 0x8C158250
WAIT = 0x8C157F60       # espera por quadro no laco do filme (thunk de 8C1745E6): conta as iteracoes
BRIGHT, OFFSET = 0x8C158D92, 0x8C158DAE  # cor base e cor de offset (fade) do sprite do filme
PVR_FB_R_CTRL, PVR_FB_R_SOF1, PVR_FB_R_SIZE = 0xA05F8044, 0xA05F8050, 0xA05F805C
TABLE = 0x8C157688


def nome(g, mid):
    try:
        ptr = struct.unpack("<I", g.read(TABLE + 12 * mid + 8, 4))[0]
        b = g.read(ptr, 16)
        return b.split(b"\0")[0].decode("latin1")
    except Exception:  # noqa: BLE001
        return "?"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("out")
    p.add_argument("--roteiro", required=True)
    p.add_argument("--passos", type=int, default=6000)
    p.add_argument("--port", type=int, default=3263)
    p.add_argument("--attach", action="store_true")
    p.add_argument("--flycast")
    p.add_argument("--game")
    p.add_argument("--limite", type=float, default=5400.0)
    p.add_argument("--capturas", type=lambda x: {int(v) for v in x.split(",")}, default=set(),
                   help="iteracoes do laco do filme (1 = primeira) em que o framebuffer e gravado")
    p.add_argument("--dir", default=".", help="pasta das capturas")
    a = p.parse_args()
    entrada = fo.EntradaBu(fo.roteiro_bu(a.roteiro))
    g = fo.connect(a)
    out = open(a.out, "w", encoding="utf-8")
    out.write("passo\tms\ttipo\tdetalhe\n")
    t0 = time.time()
    passo = 0

    def linha(tipo, det):
        out.write(f"{passo}\t{int((time.time() - t0) * 1000)}\t{tipo}\t{det}\n")
        out.flush()

    try:
        fo.enter_game(g)
        fixos = {MOVIE, GETSTAT, CREATE, START, STOP, WAIT, BRIGHT, OFFSET, fo.PD_RETORNO} | {0x80000000 | x for x in entrada.ancoras}
        for x in fixos:
            g.bp(x)
        pend = {}  # retorno -> (rotulo, r15, passo da entrada)
        ultimo_estado = None
        em_filme, iteracoes, filme_id = False, 0, -1
        ultimo_brilho = {}
        g.cont(limit=a.limite)
        while True:
            r = g.regs()
            pc = r[fo.R_PC] & 0x1FFFFFFF
            if time.time() - t0 > a.limite:
                linha("limite", f"{a.limite:.0f}s")
                break
            key = 0x80000000 | pc
            if pc in pend:
                rot, sp, p0 = pend[pc]
                if sp == r[15]:
                    del pend[pc]
                    g.bp(0x80000000 | pc, False)
                    if rot == "filme":
                        linha("retorno", f"r0={r[0]:08X} passos={passo - p0} iteracoes={iteracoes}")
                        ultimo_estado, em_filme = None, False
                    elif rot == "estado" and r[0] != ultimo_estado:
                        linha("estado", f"{r[0]:08X}")
                        ultimo_estado = r[0]
            if key == MOVIE:
                mid, ctl = r[4] & 0xFFFFFFFF, r[5] & 0xFF
                linha("entrada", f"id={mid} ctl={ctl:02X} r6={r[6]:08X} r7={r[7]:08X} {nome(g, mid) if mid < 17 else ''}")
                em_filme, iteracoes, filme_id, ultimo_brilho = True, 0, mid, {}
                pend[r[fo.R_PR] & 0x1FFFFFFF] = ("filme", r[15], passo)
                g.bp(0x80000000 | (r[fo.R_PR] & 0x1FFFFFFF))
            elif key == GETSTAT:
                ret = r[fo.R_PR] & 0x1FFFFFFF
                if ret not in pend:
                    pend[ret] = ("estado", r[15], passo)
                    g.bp(0x80000000 | ret)
            elif key == WAIT and em_filme:
                iteracoes += 1
                if iteracoes in a.capturas:
                    ctrl, sof1, size = (struct.unpack("<I", g.read(x, 4))[0] for x in (PVR_FB_R_CTRL, PVR_FB_R_SOF1, PVR_FB_R_SIZE))
                    w = ((size & 0x3FF) + 1) * 4  # bytes por linha (palavras de 32 bits)
                    h = ((size >> 10) & 0x3FF) + 1
                    mod = ((size >> 20) & 0x3FF) - 1  # salto entre linhas (palavras), entrelacado
                    nome_cap = f"filme{filme_id}_it{iteracoes:05d}"
                    raw = g.read(0xA5000000 + (sof1 & 0xFFFFFC), w * h + max(0, mod) * 4 * h)
                    open(os.path.join(a.dir, nome_cap + ".fb"), "wb").write(raw)
                    linha("captura", f"{nome_cap} ctrl={ctrl:08X} sof1={sof1:08X} size={size:08X} bytes={len(raw)}")
            elif key in (BRIGHT, OFFSET) and em_filme:
                k = "base" if key == BRIGHT else "offset"
                if ultimo_brilho.get(k) != r[5]:
                    linha(k, f"{r[5]:08X} iteracao={iteracoes}")
                    ultimo_brilho[k] = r[5]
            elif key in (CREATE, START, STOP):
                linha("sofdec", {CREATE: "create", START: "start", STOP: "stop"}[key])
            elif key == fo.PD_RETORNO:
                passo += 1
                entrada.aplica(g)
                if passo >= a.passos:
                    break
            if pc in entrada.ancoras and entrada.ancora(pc):
                linha("ancora", f"{key:08X}")
            g.step()
            if (g.regs()[fo.R_PC] & 0x1FFFFFFF) not in g.bps:
                g.cont(limit=max(1.0, a.limite - (time.time() - t0)))
        out.close()
        print(f"{passo} passos -> {a.out}")
    finally:
        fo.fecha_flycast(a, g)


if __name__ == "__main__":
    sys.exit(main())
