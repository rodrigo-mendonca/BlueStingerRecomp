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
# fase F: o gerenciador de musica (8C02AE70) e as entradas MIDI do SDK (comandos 0x01 tocar, 0x02 parar, 0x05 volume
# com tempo, 0x06 pan, 0x35 e 0x36 globais), a carga de bancos na RAM de som (8C15DDA4) e o nome dos arquivos abertos
MGER, MPLAY, MSTOP, MVOL, MPAN, M35, M36, MDOWN, GDOPEN = (0x8C02AE70, 0x8C15DBAC, 0x8C15DD40, 0x8C15DCC0, 0x8C15DC40,
                                                          0x8C15D768, 0x8C15D6A0, 0x8C15DDA4, 0x8C15BF60)
# download por tipo (8C15D534(bloco, tipo, slot)): a entrada e a volta, para medir quanto o SH-4 fica nele (DMA para a
# RAM de som); o bloco do SDK tem +8 dados e +12 tamanho
MDL = 0x8C15D534
# --injetar-musica: o topo do laco de espera do VBlank do jogo (thread principal; r0 e r2 sao recarregados ali, r14
# e preservado pela chamada) e o carregar-e-tocar do gerenciador (8C02A660(indice): banco e sequencia de 0x8C09EC28)
ESPERA_TOPO, MUS_CARREGA_TOCA = 0x8C064A26, 0x8C02A660
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


# Registradores de monitor da AICA. O MSLC (canal monitorado) e o byte alto de 0x280C (bits 13-8; o byte baixo e o
# MOBUF): escreve-se 1 byte em 0x280D. CORRIGIDO 2026-10-21: antes se escrevia em 0x2810, o MSLC nao mudava e toda
# leitura era do canal que o driver tinha escolhido por ultimo (o ca/estado das execucoes antigas nao vale).
# 0x2810 = EG (12-0), SGC (14-13), LP (15): ler o byte alto (0x2811) ou a palavra LIMPA o LP do canal (no Flycast e
# na documentacao), e o driver usa o LP para dar key-off nos efeitos (0x72D8): nos canais de efeito so o byte baixo.
MSLC, MON_EG, CA_REG = 0xA070280D, 0xA0702810, 0xA0702814


def posicao_ca(g, ch):
    """CA do canal (posicao atual na amostra, em amostras da fonte): escolhe o canal no MSLC e le o CA. No key-on visto
    numa passada, CA / razao do tom = amostras de saida desde o key-on de verdade (resolucao de uma amostra)."""
    try:
        g.write(MSLC, bytes([ch & 0x3F]))
        return struct.unpack("<H", g.read(CA_REG, 2))[0]
    except SystemExit:
        return -1


def estado_canal(g, ch, sem_limpar_lp):
    """Fase G: o estado do canal pela leitura da AICA (MSLC escolhe o canal): 0x2810 = LP (bit 15, o driver da key-off
    quando liga numa amostra sem laco, 0x72D8), SGC (14-13: 0 ataque, 1 decaimento 1, 2 decaimento 2, 3 soltura) e EG
    (12-0, atenuacao do envelope; 0x1FFF = canal parado); 0x2814 = CA (posicao na amostra). Com sem_limpar_lp (canais de
    efeito), so o byte baixo do EG: LP e SGC voltam -1 (ler o byte alto limparia o LP que o driver espera)."""
    try:
        g.write(MSLC, bytes([ch & 0x3F]))
        ca = struct.unpack("<H", g.read(CA_REG, 2))[0]
        if sem_limpar_lp:
            return -1, -1, g.read(MON_EG, 1)[0], ca
        r = struct.unpack("<H", g.read(MON_EG, 2))[0]
        return (r >> 15) & 1, (r >> 13) & 3, r & 0x1FFF, ca
    except SystemExit:
        return -1, -1, -1, -1


def tom_de(extra):
    """(amostra, oct, fns) de uma linha de canal de aica(): o que muda num pitch bend"""
    c = dict(x.split("=") for x in extra.split())
    return c["sa"], c["oct"], c["fns"]


def aica(g):
    raw = g.read(AICA, CANAIS * 0x80)
    out, extra = [], []
    for ch in range(CANAIS):
        b = ch * 0x80
        r0, r4, r8, rc, r18, r20, r24, r28 = (struct.unpack_from("<H", raw, b + o)[0]
                                              for o in (0x00, 0x04, 0x08, 0x0C, 0x18, 0x20, 0x24, 0x28))
        out.append(((r0 >> 14) & 1, (r28 >> 8) & 0xFF, (r24 >> 8) & 0xF, r24 & 0x1F))
        sa = ((r0 & 0x7F) << 16) | r4
        oct_ = (r18 >> 11) & 0xF
        extra.append(f"sa={sa:06X} lsa={r8:04X} lea={rc:04X} pcms={(r0 >> 7) & 3} loop={(r0 >> 9) & 1} "
                     f"oct={oct_ - 16 if oct_ >= 8 else oct_} fns={r18 & 0x3FF} imxl={(r20 >> 4) & 0xF} isel={r20 & 0xF}")
    return out, extra


def troca_vmu(a):
    """A VMU e o slot A1 do Flycast so nesta sessao (a mesma regra do bu-trace do flycast_oracle.py); devolve o que
    restaurar no fim."""
    import re
    import shutil
    if not a.vmu:
        return []
    base = os.path.dirname(a.flycast or fo.FLYCAST)
    vmu, cfg = os.path.join(base, "data", fo.FLYCAST_VMU), os.path.join(base, "emu.cfg")
    for f in (vmu, cfg):
        if os.path.exists(f + ".bs-original"):
            raise SystemExit(f"{f}.bs-original existe (sessao anterior interrompida?): restaure-o antes")
    trocas = [(vmu, vmu + ".bs-original"), (cfg, cfg + ".bs-original")]
    if os.path.exists(vmu):
        shutil.copyfile(vmu, vmu + ".bs-original")
    shutil.copyfile(a.vmu, vmu)
    shutil.copyfile(cfg, cfg + ".bs-original")
    txt = open(cfg, encoding="utf-8").read()
    if not re.search(r"(?m)^device1\.1 = \d+$", txt):
        restaura(trocas)
        raise SystemExit(f"{cfg}: esperava uma linha 'device1.1 = <tipo>' (slot A1)")
    open(cfg, "w", encoding="utf-8").write(re.sub(r"(?m)^device1\.1 = \d+$", "device1.1 = 1", txt))
    return trocas


def restaura(trocas):
    for f, guarda in trocas:
        if os.path.exists(guarda):
            os.replace(guarda, f)
        elif os.path.exists(f):
            os.remove(f)


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
    p.add_argument("--dump-dsp", type=int, default=-1, help="passo em que os registradores da AICA de 0x2000 a 0x45FF "
                   "(mixer de saida do DSP EFSDL/EFPAN, RBL/RBP em 0x2804, COEF, MADRS, MPRO, TEMP, MEMS, MIXS, EFREG) sao "
                   "gravados (fase G: o DSP)")
    p.add_argument("--seguir-sa", default="", help="SA,... (hex, endereco na RAM de som): a cada passo, os canais com "
                   "KYONB tocando essas amostras: LP, SGC, EG e CA (fase G: o key-off do driver pelo LP)")
    p.add_argument("--ticks", action="store_true", help="a cada passo: o contador de ticks reais do driver (ARM 0x13418, "
                   "250 Hz no tratador) e a trilha 0 (ponteiro de leitura - base do SMSD, contagem): a prova direta de ticks "
                   "perdidos (docs/N6-PLANO.md 18.4)")
    p.add_argument("--injetar-musica", default="", help="PASSO:INDICE - MEDIDA so no oraculo: no topo da espera do "
                   "VBlank depois de PASSO, chama 8C02A660(INDICE) (como o script de eventos faria) e volta ao laco")
    p.add_argument("--vmu", help="imagem de VMU (128 KB) desta sessao (p.ex. com o save de gameplay do N4); a do Flycast volta no fim")
    p.add_argument("--ca", action="store_true", help="no key-on, a posicao CA do canal (o instante exato do key-on)")
    p.add_argument("--musica", action="store_true", help="fase F: gerenciador de musica, entradas MIDI, bancos e arquivos")
    p.add_argument("--falas", action="store_true", help="fase E: pedidos de fala e o MW WAV (estado so quando muda)")
    p.add_argument("--posicao", type=int, default=0, help="a cada N passadas, a posicao do Eliot (0x8C24D668)")
    p.add_argument("--trocar", default="", help="banco:pedido por som tocado, separados por virgula")
    p.add_argument("--forcar", default="", help="cmd:valor por som tocado (15 ou 16), separados por virgula")
    a = p.parse_args()
    entrada = fo.EntradaBu(fo.roteiro_bu(a.roteiro))
    trocas = troca_vmu(a)
    try:
        g = fo.connect(a)
    except BaseException:
        restaura(trocas)
        raise
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
        musica = {MGER, MPLAY, MSTOP, MVOL, MPAN, M35, M36, MDOWN, GDOPEN, MDL} if a.musica else set()
        dlpend = {}  # volta do download: endereco -> (sp, tipo, tmu da entrada)
        seguir = {int(x, 16) for x in a.seguir_sa.split(",") if x}
        seguidos = {}  # canal -> ainda seguindo (tocando ou soltando)
        falas = falas | musica
        wpend = {}           # retorno do estado do MW WAV -> r15
        westado = None
        for x in {QUEUE, STOP, PLAY, P15, P16, STAT, fo.PD_RETORNO} | falas | {0x80000000 | x for x in entrada.ancoras}:
            g.bp(x)
        pend = {}            # retorno -> (r15, ponteiro de saida, porta)
        estado = {}          # porta -> ultima palavra +24
        aica_ant = None
        extra_ant = None
        comando = False      # houve tocar/parametro nesta passada
        trocar = [(int(x.split(":")[0]), int(x.split(":")[1])) for x in a.trocar.split(",") if x]
        forcar = [(int(x.split(":")[0]), int(x.split(":")[1])) for x in a.forcar.split(",") if x]
        tocados = 0
        apos_tocar = 0
        injetar = tuple(int(x) for x in a.injetar_musica.split(":")) if a.injetar_musica else None
        injetado = False
        g.cont(limit=a.limite)
        while True:
            r = g.regs()
            pc = r[fo.R_PC] & 0x1FFFFFFF
            if time.time() - t0 > a.limite:
                linha("limite", f"{a.limite:.0f}s")
                break
            key = 0x80000000 | pc
            if injetar and not injetado and key == ESPERA_TOPO:
                # chamada simulada: PR = o topo do laco (a volta recarrega r0/r2), PC = carregar-e-tocar, r4 = indice
                g.bp(ESPERA_TOPO, False)
                g.set_reg(fo.R_PR, ESPERA_TOPO)
                g.set_reg(4, injetar[1])
                g.set_reg(fo.R_PC, MUS_CARREGA_TOCA)
                injetado = True
                linha("injetado", f"8C02A660({injetar[1]}) {tmu(g)}")
                r = g.regs()
                pc = r[fo.R_PC] & 0x1FFFFFFF
                key = 0x80000000 | pc
            if pc in wpend and wpend[pc] == r[15]:
                wpend.pop(pc)
                g.bp(0x80000000 | pc, False)
                if r[0] != westado:  # so as mudancas (o jogo consulta a cada quadro)
                    westado = r[0]
                    linha("fala-estado", f"{s8(r[0])} {tmu(g)}")
            if pc in dlpend and dlpend[pc][0] == r[15]:
                _, tipo, tdl = dlpend.pop(pc)
                g.bp(0x80000000 | pc, False)
                t1 = struct.unpack("<I", g.read(0xFFD8000C, 4))[0]
                linha("mus-download-fim", f"tipo={tipo} r0={r[0]:08X} duracao={((tdl - t1) & 0xFFFFFFFF) / 781250 * 1000:.3f} ms {tmu(g)}")
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
            elif key in musica:
                t = tmu(g)
                if key == GDOPEN:
                    nome = g.read(r[4], 24).split(b"\0")[0].decode("latin1") if r[4] else "-"
                    linha("arquivo", f"{nome} {t}")
                elif key == MGER:
                    linha("mus-gerente", f"v={s8(r[4])} t={r[5] & 0xFFFF} {t}")
                elif key == MPLAY:
                    linha("mus-tocar", f"porta={porta(g, r[4])} banco={s8(r[5])} seq={s8(r[6])} x={s8(r[7])} {t}")
                elif key == MSTOP:
                    linha("mus-parar", f"porta={porta(g, r[4])} {t}")
                elif key in (MVOL, MPAN):
                    linha("mus-vol" if key == MVOL else "mus-pan", f"porta={porta(g, r[4])} v={s8(r[5])} t={r[6] & 0xFFFF} {t}")
                elif key == MDL:
                    tipo = struct.pack("<I", r[5]).decode("latin1")
                    tam = struct.unpack("<I", g.read(r[4] + 12, 4))[0] if r[4] else 0
                    ret = r[fo.R_PR] & 0x1FFFFFFF
                    tdl = struct.unpack("<I", g.read(0xFFD8000C, 4))[0]  # (nao e o t0 do relogio do script)
                    if ret not in dlpend:
                        dlpend[ret] = (r[15], tipo, tdl)
                        g.bp(0x80000000 | ret)
                    linha("mus-download", f"tipo={tipo} slot={r[6] & 0xFF} bytes={tam} {t}")
                elif key in (M35, M36):
                    linha("mus-35" if key == M35 else "mus-36", f"r4={r[4]:08X} r5={r[5]:08X} r6={r[6]:08X} {t}")
                else:  # MDOWN: carga de unidades na RAM de som
                    linha("mus-carga", f"r4={r[4]:08X} r5={r[5]:08X} r6={r[6]:08X} r7={r[7]:08X} {t}")
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
                    mud = [f"c{ch}:k{k} tl{tl} sdl{sdl} pan{pan}" + (f" [{extra[ch]}" + (f" ca={posicao_ca(g, ch)}" if a.ca else "") + "]"
                                                                        if k and not aica_ant[ch][0] else "")
                           for ch, (k, tl, sdl, pan) in enumerate(cur) if cur[ch] != aica_ant[ch]]
                    if mud:
                        linha("aica", ("cmd " if comando else "") + " ".join(mud) + f" {tmu(g)}")
                    # tom novo num canal que segue com KYONB nas duas leituras, MESMA amostra e OCT/FNS diferentes (o
                    # pitch bend reescreve so o tom). A leitura por passo nao ve o KYONEX: um re-disparo da mesma amostra
                    # entre as leituras nao fica excluido (revisao Codex 2026-10-20); o bend se prova pelos valores.
                    tons = [f"c{ch}:[{extra[ch]}]" for ch in range(CANAIS)
                            if cur[ch][0] and aica_ant[ch][0] and extra_ant and tom_de(extra[ch])[0] == tom_de(extra_ant[ch])[0]
                            and tom_de(extra[ch])[1:] != tom_de(extra_ant[ch])[1:]]
                    if tons:
                        linha("tom", " ".join(tons) + f" {tmu(g)}")
                aica_ant, extra_ant = cur, extra
                if seguir:
                    # com KYONB e depois do key-off tambem (a soltura), ate o envelope chegar ao silencio (EG 0x1FFF)
                    for ch in range(CANAIS):
                        if int(tom_de(extra[ch])[0], 16) in seguir and (cur[ch][0] or seguidos.get(ch)):
                            lp, sgc, eg, ca = estado_canal(g, ch, ch >= 48)  # efeitos nos canais 48+ (driver)
                            # efeito: so enquanto tocar (sem o EG inteiro); musica: ate o envelope chegar ao silencio
                            seguidos[ch] = bool(cur[ch][0]) or (ch < 48 and 0 <= eg < 0x1FFF)
                            linha("seguir", f"c{ch} sa={tom_de(extra[ch])[0]} k={cur[ch][0]} lp={lp} sgc={sgc} eg={eg} ca={ca} {tmu(g)}")
                if passo == a.dump_dsp:
                    dest = os.path.splitext(a.out)[0] + f"_dsp_{passo}.bin"
                    open(dest, "wb").write(g.read(0xA0702000, 0x2600))  # 0x2000-0x45FF
                    linha("dsp", dest)
                if passo == a.dump_sram:
                    dest = os.path.splitext(a.out)[0] + f"_sram_{passo}.bin"
                    open(dest, "wb").write(g.read(0xA0800000, 0x200000))
                    linha("sram", dest)
                comando = False
                if injetar and not injetado and passo == injetar[0]:
                    g.bp(ESPERA_TOPO)  # a partir daqui, a proxima volta da espera do VBlank recebe a chamada
                if a.ticks:
                    tk = struct.unpack("<I", g.read(0xA0800000 + 0x13418, 4))[0]
                    tr = g.read(0xA0800000 + 0x10000, 0x40)
                    ptr, base, cnt = (struct.unpack_from("<I", tr, o)[0] for o in (0x10, 0x14, 0x24))
                    linha("ticks", f"t={tk} p={(ptr - base) & 0xFFFFFFFF} c={struct.unpack('<i', struct.pack('<I', cnt))[0]} "
                                   f"est={tr[1]} {tmu(g)}")
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
        restaura(trocas)


if __name__ == "__main__":
    sys.exit(main())
