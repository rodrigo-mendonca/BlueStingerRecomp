"""Oraculo O6 do N6 (docs/N6-PLANO.md, secao 11, fase B): as 5 entradas do SDK de som no Flycast.

Registra, por passo de jogo (passada do servidor pd, como o n5_oraculo e o bu-trace):
  fila      8C02A6B8(id = r4, a = r5, b = r6): o pedido do jogo (antes da selecao por prioridade)
  parar     8C15E208(h)                     porta = byte 1 do handle
  tocar     8C15E088(h, banco, pedido, x)   r5, r6, r7 com sinal (byte)
  p15/p16   8C15E19C / 8C15E11C (h, v, t)   v com sinal (byte), t 16 bits
  estado    8C15DF44(h, out): a palavra +24 da saida no retorno, so quando muda (por porta)
  aica      a cada passada, os canais da AICA cujo
            KYONB, TL, DISDL ou DIPAN mudaram (0xA0700000 + canal*0x80: +0x00 bit 14, +0x28 bits 15-8,
            +0x24 bits 11-8 e 4-0). E o efeito que o driver produz: liga o valor de 0x15/0x16 a volume ou pan
            sem presumir. No key-on (KYONB 0 -> 1) tambem a amostra: SA (+0x00 bits 6-0 e +0x04), LSA (+0x08),
            LEA (+0x0C), PCMS (+0x00 bits 8-7), LPCTL (+0x00 bit 9), OCT/FNS (+0x18): onde o som esta na RAM de som
            (prova do par (banco, pedido) -> amostra do BS_SND.MLT, fase C).
  --forcar SEQ    MEDIDA so no oraculo (o jogo recompilado nao muda): no k-esimo "tocar", troca o valor (r5) do
                  comando de parametro indicado antes de ele executar, p.ex. "15:-64,16:-64,15:32": som 1 com 0x15 = -64,
                  som 2 com 0x16 = -64, ... O log "aica" da mesma passada mostra se mudou DIPAN (pan) ou TL/DISDL (nivel).
  --trocar SEQ    MEDIDA so no oraculo: no k-esimo "tocar", troca (banco, pedido) antes de ele executar, p.ex.
                  "0:11,0:67,0:37": o som 1 vira o pedido 11 do banco 0, ... O key-on mostra LPCTL/LSA/LEA da amostra
                  e o log "aica" mostra se o canal desliga sozinho (sem laco) ou so com o parar (laco).
  --dump-sram P   grava a RAM de som (2 MB, 0xA0800000) no passo P (depois do sd_multi_unit_download), para
                  localizar as unidades do MLT e as amostras.
Entrada injetada pelo roteiro "indice passo" (com @ancora/@pulsos), a mesma do bu-trace. Nada e escrito no jogo.

    python tools/dev/n6_oraculo.py <saida.tsv> --roteiro <r> [--passos N] [--game <cdi>]
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flycast_oracle as fo  # noqa: E402

# fase E: o pedido de fala do jogo (8C08B860, id) e os metodos do MW WAV (start, estado, stop, destroy)
VPEDIDO, WSTART, WSTAT, WSTOP, WDESTROY = 0x8C08B860, 0x8C1591C8, 0x8C159340, 0x8C1592A0, 0x8C159134
VBCNT = 0x8C1DD5B4  # contador do callback de VBlank 8C172602: NAO serve (fica parado depois do boot; 2026-10-20)
TMU_TCR, TMU_TCNT = (0xFFD80010, 0xFFD8001C, 0xFFD80028), (0xFFD8000C, 0xFFD80018, 0xFFD80024)


def tmu(g):
    """Tempo emulado: os 3 TCNT (contam para baixo) e o prescaler de cada canal (TCR bits 2-0). O canal livre (TCOR
    = 0xFFFFFFFF) da a duracao entre duas linhas sem depender de quantos VBlanks tem um passo."""
    try:
        c = [struct.unpack("<I", g.read(a, 4))[0] for a in TMU_TCNT]
        r = [struct.unpack("<H", g.read(a, 2))[0] & 7 for a in TMU_TCR]
    except SystemExit:
        return "tmu=?"
    return "tmu=" + ",".join(f"{x:08X}/{p}" for x, p in zip(c, r))
QUEUE, STOP, PLAY, P15, P16, STAT = 0x8C02A6B8, 0x8C15E208, 0x8C15E088, 0x8C15E19C, 0x8C15E11C, 0x8C15DF44
AICA, CANAIS = 0xA0700000, 64


def s8(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


def porta(g, h):
    if h == 0:
        return "nulo"
    try:
        return str(g.read(h + 1, 1)[0])
    except SystemExit:
        return "?"


def aica(g):
    raw = g.read(AICA, CANAIS * 0x80)
    out, extra = [], []
    for ch in range(CANAIS):
        b = ch * 0x80
        r0, r4, r8, rc, r18, r24, r28 = (struct.unpack_from("<H", raw, b + o)[0] for o in (0x00, 0x04, 0x08, 0x0C, 0x18, 0x24, 0x28))
        out.append(((r0 >> 14) & 1, (r28 >> 8) & 0xFF, (r24 >> 8) & 0xF, r24 & 0x1F))
        sa = ((r0 & 0x7F) << 16) | r4
        oct_ = (r18 >> 11) & 0xF
        extra.append(f"sa={sa:06X} lsa={r8:04X} lea={rc:04X} pcms={(r0 >> 7) & 3} loop={(r0 >> 9) & 1} "
                     f"oct={oct_ - 16 if oct_ >= 8 else oct_} fns={r18 & 0x3FF}")
    return out, extra


def main():
    p = argparse.ArgumentParser()
    p.add_argument("out")
    p.add_argument("--roteiro", required=True)
    p.add_argument("--passos", type=int, default=12000)
    p.add_argument("--port", type=int, default=3263)
    p.add_argument("--attach", action="store_true")
    p.add_argument("--flycast")
    p.add_argument("--game")
    p.add_argument("--limite", type=float, default=7200.0)
    p.add_argument("--dump-sram", type=int, default=-1, help="passo em que a RAM de som (2 MB) e gravada")
    p.add_argument("--falas", action="store_true", help="fase E: pedidos de fala e o MW WAV (estado so quando muda)")
    p.add_argument("--posicao", type=int, default=0, help="a cada N passadas, a posicao do Eliot (0x8C24D668)")
    p.add_argument("--trocar", default="", help="banco:pedido por som tocado, separados por virgula")
    p.add_argument("--forcar", default="", help="cmd:valor por som tocado (15 ou 16), separados por virgula")
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
        falas = {VPEDIDO, WSTART, WSTAT, WSTOP, WDESTROY} if a.falas else set()
        wpend = {}           # retorno do estado do MW WAV -> r15
        westado = None
        for x in {QUEUE, STOP, PLAY, P15, P16, STAT, fo.PD_RETORNO} | falas | {0x80000000 | x for x in entrada.ancoras}:
            g.bp(x)
        pend = {}            # retorno -> (r15, ponteiro de saida, porta)
        estado = {}          # porta -> ultima palavra +24
        aica_ant = None
        comando = False      # houve tocar/parametro nesta passada
        trocar = [(int(x.split(":")[0]), int(x.split(":")[1])) for x in a.trocar.split(",") if x]
        forcar = [(int(x.split(":")[0]), int(x.split(":")[1])) for x in a.forcar.split(",") if x]
        tocados = 0
        apos_tocar = 0
        g.cont(limit=a.limite)
        while True:
            r = g.regs()
            pc = r[fo.R_PC] & 0x1FFFFFFF
            if time.time() - t0 > a.limite:
                linha("limite", f"{a.limite:.0f}s")
                break
            key = 0x80000000 | pc
            if pc in wpend and wpend[pc] == r[15]:
                wpend.pop(pc)
                g.bp(0x80000000 | pc, False)
                if r[0] != westado:  # so as mudancas (o jogo consulta a cada quadro)
                    westado = r[0]
                    linha("fala-estado", f"{s8(r[0])} {tmu(g)}")
            if pc in pend and pend[pc][0] == r[15]:
                _, saida, pt = pend.pop(pc)
                g.bp(0x80000000 | pc, False)
                if r[0] == 0 and saida:
                    w = struct.unpack("<I", g.read(saida + 24, 4))[0]
                    if estado.get(pt) != w:
                        linha("estado", f"porta={pt} +24={w:08X}")
                        estado[pt] = w
                elif r[0]:
                    linha("estado", f"porta={pt} erro={r[0]:08X}")
            if key == QUEUE:
                linha("fila", f"id={r[4] & 0xFFFF} banco={(r[4] & 0xFFFF) // 128} pedido={(r[4] & 0xFFFF) % 128} a={s8(r[5])} b={s8(r[6])}")
            elif key == STOP:
                linha("parar", f"porta={porta(g, r[4])}")
            elif key == PLAY:
                if tocados < len(trocar):
                    bk, pd = trocar[tocados]
                    linha("trocado", f"som={tocados + 1} banco={bk} pedido={pd} (era {s8(r[5])}:{s8(r[6])})")
                    g.set_reg(5, bk & 0xFFFFFFFF)
                    g.set_reg(6, pd & 0xFFFFFFFF)
                    r = g.regs()
                linha("tocar", f"porta={porta(g, r[4])} banco={s8(r[5])} pedido={s8(r[6])} x={s8(r[7])} {tmu(g)}")
                comando = True
                tocados += 1
                apos_tocar = 2  # so os 2 comandos de parametro logo depois deste tocar (0x15 e 0x16 do consumidor)
            elif key in (P15, P16):
                cmd = 15 if key == P15 else 16
                apos = apos_tocar > 0
                apos_tocar = max(0, apos_tocar - 1)
                if apos and 0 < tocados <= len(forcar) and forcar[tocados - 1][0] == cmd and r[6] & 0xFFFF == 0 and s8(r[5]) != forcar[tocados - 1][1]:
                    g.set_reg(5, forcar[tocados - 1][1] & 0xFFFFFFFF)
                    linha("forcado", f"som={tocados} cmd=0x{cmd} v={forcar[tocados - 1][1]} (era {s8(r[5])})")
                    r = g.regs()
                linha("p15" if key == P15 else "p16", f"porta={porta(g, r[4])} v={s8(r[5])} t={r[6] & 0xFFFF}")
                comando = True
            elif key in falas:
                vb = tmu(g)
                if key == VPEDIDO:
                    linha("fala-pedido", f"id={r[4] & 0xFFFF} {vb}")
                elif key == WSTART:
                    nome = g.read(r[5], 16).split(b"\0")[0].decode("latin1")
                    linha("fala-start", f"nome={nome} h={r[4]:08X} {vb}")
                    westado = None
                elif key == WSTAT:
                    ret = r[fo.R_PR] & 0x1FFFFFFF
                    if ret not in wpend:
                        wpend[ret] = r[15]
                        g.bp(0x80000000 | ret)
                else:
                    linha("fala-stop" if key == WSTOP else "fala-destroy", f"h={r[4]:08X} {vb}")
            elif key == STAT:
                ret = r[fo.R_PR] & 0x1FFFFFFF
                if ret not in pend:
                    pend[ret] = (r[15], r[5], porta(g, r[4]))
                    g.bp(0x80000000 | ret)
            elif key == fo.PD_RETORNO:
                # toda passada (so as diferencas vao para o log): a mudanca causada por um comando aparece na
                # passada em que o driver a aplica, com a anterior como base
                cur, extra = aica(g)
                if aica_ant is not None:
                    mud = [f"c{ch}:k{k} tl{tl} sdl{sdl} pan{pan}" + (f" [{extra[ch]}]" if k and not aica_ant[ch][0] else "")
                           for ch, (k, tl, sdl, pan) in enumerate(cur) if cur[ch] != aica_ant[ch]]
                    if mud:
                        linha("aica", ("cmd " if comando else "") + " ".join(mud) + f" {tmu(g)}")
                aica_ant = cur
                if passo == a.dump_sram:
                    dest = os.path.splitext(a.out)[0] + f"_sram_{passo}.bin"
                    open(dest, "wb").write(g.read(0xA0800000, 0x200000))
                    linha("sram", dest)
                comando = False
                if a.posicao and passo % a.posicao == 0:
                    x, y, z = struct.unpack("<fff", g.read(0x8C24D668, 12))
                    linha("posicao", f"x={x:.3f} y={y:.3f} z={z:.3f}")
                passo += 1
                if entrada.ancora_ram(lambda ad: struct.unpack("<I", g.read(ad, 4))[0]):
                    linha("ancora", f"ram passo_entrada={entrada.passo}")
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
