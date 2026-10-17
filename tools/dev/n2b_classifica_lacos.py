# N2b P0 (docs/N2b-ESPERAS.md): classifica os lacos sem store e sem chamada da saida de
# n2b_lacos_quentes.py pelo tipo de leitura de memoria do corpo:
#   - "sem leitura": so registradores (contador, aritmetica). Nao pode esperar estado externo;
#   - "ponteiro movel": toda leitura usa base (ou indice R0) escrita dentro do laco. E uma
#     travessia de dados, que termina pela propria contagem ou pelo dado lido;
#   - "endereco fixo": alguma leitura tem base e indice que o laco nao escreve. Le a mesma
#     posicao a cada volta: candidato a espera por bandeira; revisar a mao.
# Literais do pool (mov.l @(d,PC)) sao constantes e nao contam como leitura.
# Uso: n2b_classifica_lacos.py BIN lacos.tsv
import struct, sys
BASE = 0x8C010000
d = open(sys.argv[1], 'rb').read()
END = BASE + len(d)
w16 = lambda a: struct.unpack_from('<H', d, a - BASE)[0] if BASE <= a < END - 1 else 0


def body_end(b):
    """Fim (exclusivo) do corpo: inclui o delay slot so se o back-edge tiver um (bt/s, bf/s, bra)."""
    op = w16(b)
    delayed = (op >> 12) == 0xA or ((op >> 12) == 0x8 and ((op >> 8) & 15) in (0xD, 0xF))
    return b + 4 if delayed else b + 2


def decode(op):
    """(registradores escritos, leituras [(base, indice ou None)])."""
    hi, n, m, lo = op >> 12, (op >> 8) & 15, (op >> 4) & 15, op & 15
    wr, rd = set(), []
    if hi == 0x6:
        wr.add(n)
        if lo in (0, 1, 2):
            rd.append((m, None))
        if lo in (4, 5, 6):
            rd.append((m, None)); wr.add(m)
    elif hi == 0x5:
        wr.add(n); rd.append((m, None))
    elif hi == 0x0 and lo in (0xC, 0xD, 0xE):
        wr.add(n); rd.append((m, 0))
    elif hi == 0x0 and (op & 0xFF) in (0x29, 0x0A, 0x1A, 0x2A, 0x5A, 0x6A, 0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x82, 0xFA, 0x3A, 0x4A):
        wr.add(n)
    elif hi == 0x8 and n in (4, 5):
        wr.add(0); rd.append((m, None))
    elif hi == 0xC and n in (4, 5, 6):
        wr.add(0); rd.append(('gbr', None))
    elif hi == 0xC and n in (7, 9, 0xA, 0xB):
        wr.add(0)
    elif hi == 0xC and n in (0xC, 0xD, 0xE, 0xF):  # tst.b/and.b/xor.b/or.b #imm,@(R0,GBR)
        rd.append(('gbr', 0))
    elif hi in (0x7, 0xE, 0x9, 0xD):
        wr.add(n)
    elif hi == 0x3 and lo not in (0, 2, 3, 6, 7):
        wr.add(n)
    elif hi == 0x2 and lo in (9, 0xA, 0xB, 0xD):
        wr.add(n)
    elif hi == 0x4:
        if (op & 0xFF) in (0x10, 0x00, 0x01, 0x08, 0x09, 0x18, 0x19, 0x28, 0x29, 0x20, 0x21, 0x24, 0x25, 0x04, 0x05):
            wr.add(n)
        elif lo in (0xC, 0xD):
            wr.add(n)
        elif (op & 0xFF) in (0x06, 0x16, 0x26, 0x56, 0x66, 0x07, 0x17, 0x27, 0x37, 0x47, 0xF6, 0xF7):  # lds.l/ldc.l @Rm+
            wr.add(n); rd.append((n, None))
        elif lo == 0xF:  # mac.w
            wr.update((n, m)); rd.extend([(n, None), (m, None)])
    elif hi == 0x0 and lo == 0xF:  # mac.l
        wr.update((n, m)); rd.extend([(n, None), (m, None)])
    elif hi == 0xF:
        if lo in (8, 9):
            rd.append((m, None))
            if lo == 9:
                wr.add(m)
        elif lo == 6:
            rd.append((m, 0))
    return wr, rd


def classify(t, b):
    # Registrador escrito so por constante (literal do pool, mov #imm, mova) nao "anda": a base
    # recarregada a cada volta continua sendo o mesmo endereco.
    wr, reads = set(), []
    for x in range(t, body_end(b), 2):
        op = w16(x)
        w, r = decode(op)
        if (op >> 12) in (0x9, 0xD, 0xE) or (op >> 8) == 0xC7:
            w = set()
        wr |= w
        reads += r
    if not reads:
        return 'sem leitura'
    fixed = [r for r in reads if r[0] not in wr and (r[1] is None or r[1] not in wr)]
    return 'endereco fixo' if fixed else 'ponteiro movel'


rows = [l.rstrip('\n').split('\t') for l in open(sys.argv[2], encoding='utf-8-sig')]
print('\t'.join(rows[0] + ['leitura']))
seen = set()
for r in rows[1:]:
    if len(r) < 7 or r[5] != 'NAO' or r[6] != 'NAO' or (r[1], r[2]) in seen:
        continue
    seen.add((r[1], r[2]))
    print('\t'.join(r + [classify(int(r[1], 16), int(r[2], 16))]))
