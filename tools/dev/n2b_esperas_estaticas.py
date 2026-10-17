# N2b P0 (docs/N2b-PROPOSTA.md, secao 9.1; relatorio em docs/N2b-ESPERAS.md).
# Inventario estatico de lacos de espera (P0): desvios para tras cujo corpo le hardware temporal
# ou de estado assincrono, ou chama syscalls do BIOS/funcoes que leem hardware.
# Uso: esperas_estaticas.py 1ST_READ.BIN > saida.tsv
import struct, sys

BASE = 0x8C010000
data = open(sys.argv[1], 'rb').read()
END = BASE + len(data)


def w16(a):
    return struct.unpack_from('<H', data, a - BASE)[0] if BASE <= a < END - 1 else None


def w32(a):
    return struct.unpack_from('<I', data, a - BASE)[0] if BASE <= a < END - 3 else None


def s8(x):
    return x - 256 if x & 0x80 else x


def s12(x):
    return x - 4096 if x & 0x800 else x


def region(v):
    if v is None:
        return None
    p = v & 0x1FFFFFFF if v < 0xE0000000 else v
    if 0x005F810C <= p <= 0x005F810F: return 'SPG_STATUS'
    if 0x005F80C8 <= p <= 0x005F80F4: return 'spg'
    if 0x005F6900 <= p <= 0x005F693F: return 'holly_intc'
    if 0x005F6800 <= p <= 0x005F68FF: return 'holly_sb_dma'
    if 0x005F6C00 <= p <= 0x005F6CFF: return 'maple'
    if 0x005F7000 <= p <= 0x005F70FF: return 'gdrom'
    if 0x005F7400 <= p <= 0x005F74FF: return 'g1'
    if 0x005F7800 <= p <= 0x005F78FF: return 'g2_dma'
    if 0x005F7C00 <= p <= 0x005F7CFF: return 'pvr_dma'
    if 0x005F8000 <= p <= 0x005F9FFF: return 'pvr'
    if 0x00700000 <= p <= 0x0070FFFF: return 'aica_regs'
    if 0x00710000 <= p <= 0x0071000F: return 'aica_rtc'
    if 0x00800000 <= p <= 0x009FFFFF: return 'aica_ram'
    if 0xFFD80000 <= p <= 0xFFD8FFFF: return 'tmu'
    if 0xFFC80000 <= p <= 0xFFC8FFFF: return 'rtc_sh4'
    if 0xFFA00000 <= p <= 0xFFA0FFFF: return 'dmac'
    if 0xFFD00000 <= p <= 0xFFD0FFFF: return 'intc'
    if 0xFF000000 <= p <= 0xFF00FFFF: return 'ccn'
    if 0x8C000000 <= v <= 0x8C00FFFF and v & 0xFF in (0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xE0): return 'bios_syscall'
    return None


def regs_before(t, n=24):
    """Rastreia constantes de registradores de t-2n ate t (heuristica)."""
    regs = {}
    a = t - 2 * n
    while a < t:
        step(a, regs)
        a += 2
    return regs


def step(a, regs):
    """Atualiza regs com a instrucao em a; devolve (tipo, alvo/base, reg_destino)."""
    op = w16(a)
    if op is None:
        return None
    hi, n, m, lo = op >> 12, (op >> 8) & 15, (op >> 4) & 15, op & 15
    if hi == 0xD:  # mov.l @(disp,PC),Rn
        regs[n] = w32((a & ~3) + 4 + (op & 0xFF) * 4)
        return None
    if hi == 0x9:  # mov.w @(disp,PC),Rn
        v = w16(a + 4 + (op & 0xFF) * 2)
        regs[n] = (v - 0x10000 if v & 0x8000 else v) & 0xFFFFFFFF if v is not None else None
        return None
    if hi == 0xE:
        regs[n] = s8(op & 0xFF) & 0xFFFFFFFF
        return None
    if hi == 0x7:
        if regs.get(n) is not None:
            regs[n] = (regs[n] + s8(op & 0xFF)) & 0xFFFFFFFF
        return None
    if hi == 0x6 and lo == 0x3:  # mov Rm,Rn
        regs[n] = regs.get(m)
        return None
    if hi == 0x6 and lo in (0, 1, 2):  # mov.x @Rm,Rn
        base = regs.get(m)
        regs[n] = None
        return ('load', base)
    if hi == 0x6 and lo in (4, 5, 6):  # mov.x @Rm+,Rn
        base = regs.get(m)
        regs[n] = None
        return ('load', base)
    if hi == 0x5:  # mov.l @(disp,Rm),Rn
        base = regs.get(m)
        regs[n] = None
        return ('load', None if base is None else (base + lo * 4) & 0xFFFFFFFF)
    if hi == 0x8 and n in (4, 5):  # mov.b/w @(disp,Rm),R0
        base = regs.get(m)
        regs[0] = None
        return ('load', None if base is None else (base + (op & 15) * (1 if n == 4 else 2)) & 0xFFFFFFFF)
    if hi == 0x0 and lo in (0xC, 0xD, 0xE):  # mov.x @(R0,Rm),Rn
        base = regs.get(m)
        r0 = regs.get(0)
        regs[n] = None
        return ('load', None if base is None or r0 is None else (base + r0) & 0xFFFFFFFF)
    if hi == 0x4 and lo == 0xB and m == 0:  # jsr @Rn
        return ('call', regs.get(n))
    if hi == 0xB:
        return ('call', a + 4 + s12(op & 0xFFF) * 2)
    # escritas genericas em Rn: invalida
    if hi in (0x2, 0x3, 0x6) or (hi == 0x4 and lo not in (0xB,)) or (hi == 0x0 and lo in (2, 9, 0xA)):
        if not (hi == 0x2 and lo in (0, 1, 2, 4, 5, 6)):  # stores nao escrevem Rn
            regs[n] = None
    return None


# funcoes (alvos de chamada) que leem hardware diretamente nas primeiras 48 instrucoes
def func_reads_hw(f, memo={}):
    if f in memo:
        return memo[f]
    memo[f] = None
    if f is None or not (BASE <= f < END):
        r = region(f)
        memo[f] = r if r == 'bios_syscall' else None
        return memo[f]
    regs = {}
    a = f
    found = None
    for _ in range(48):
        op = w16(a)
        if op is None:
            break
        res = step(a, regs)
        if res and res[0] == 'load' and region(res[1]) and region(res[1]) != 'ccn':
            found = region(res[1])
            break
        if op == 0x000B:  # rts
            break
        a += 2
    memo[f] = found
    return found


out = []
for a in range(BASE, END - 1, 2):
    op = w16(a)
    hi = op >> 12
    if hi == 0x8 and ((op >> 8) & 15) in (9, 0xB, 0xD, 0xF):
        t = a + 4 + s8(op & 0xFF) * 2
    elif hi == 0xA:
        t = a + 4 + s12(op & 0xFFF) * 2
    else:
        continue
    if t > a or a - t > 96:
        continue
    regs = regs_before(t)
    hits, calls = set(), set()
    x = t
    while x <= a + 2:
        res = step(x, regs)
        if res and res[0] == 'load':
            r = region(res[1])
            if r and r != 'ccn':
                hits.add(r)
        if res and res[0] == 'call':
            r = func_reads_hw(res[1])
            if r:
                calls.add(f'{r}@{res[1]:08X}' if res[1] is not None else r)
        x += 2
    if hits or calls:
        out.append((t, a, a - t + 2, ','.join(sorted(hits)), ','.join(sorted(calls))))

print('inicio\tbackedge\tbytes\tle_hw\tchama_hw')
for t, a, nb, h, c in out:
    print(f'{t:08X}\t{a:08X}\t{nb}\t{h}\t{c}')
