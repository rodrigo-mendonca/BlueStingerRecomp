# N2b P0 (docs/N2b-PROPOSTA.md, secao 9.1; relatorio em docs/N2b-ESPERAS.md).
# Cruza as voltas exatas por back-edge (BS_WW_VOLTAS: pc, voltas, retornos, sem progresso) ou o perfil
# indicativo por PC (BS_PROGRESSO_PCS: thread, pc, trabalho) com TODOS os desvios para tras do
# binario (bt, bf, bt/s, bf/s, bra), sem limite de tamanho. Para cada laco executado lista o
# inicio, o back-edge, o tamanho, as voltas e se o corpo escreve na memoria (store) ou chama
# funcoes. Um laco sem store e sem chamada que le memoria e candidato a espera por bandeira.
# Os filtros sao so de prioridade (padrao: nenhum).
# Uso: n2b_lacos_quentes.py BIN perfil.tsv [minimo_voltas=1] [max_bytes=sem limite]
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


prof = {}
for l in open(sys.argv[2]):
    f = l.strip().split('\t')
    if len(f) in (2, 3, 4) and len(f[0]) == 8:  # BS_WW_VOLTAS: pc, voltas[, retornos[, sem progresso]]
        if int(f[1]):
            prof[(-1, int(f[0], 16))] = (int(f[1]), int(f[3]) if len(f) > 3 else -1)
    elif len(f) == 3:  # BS_PROGRESSO_PCS: thread, pc, trabalho
        prof[(int(f[0]), int(f[1], 16))] = (int(f[2]), -1)
minimo = int(sys.argv[3]) if len(sys.argv) > 3 else 1
max_bytes = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 30
loops = {}
for a in range(BASE, END - 1, 2):
    op = w16(a)
    hi = op >> 12
    if hi == 0x8 and ((op >> 8) & 15) in (9, 0xB, 0xD, 0xF):
        disp = op & 0xFF
        disp = disp - 256 if disp & 0x80 else disp
    elif hi == 0xA:
        disp = op & 0xFFF
        disp = disp - 4096 if disp & 0x800 else disp
    else:
        continue
    t = a + 4 + disp * 2
    if t <= a and a - t <= max_bytes:
        loops.setdefault(t, []).append(a)


def has_store(t, b):
    for x in range(t, body_end(b), 2):
        op = w16(x)
        hi, lo = op >> 12, op & 15
        if hi == 0x2 and lo in (0, 1, 2, 4, 5, 6):  # mov.x Rm,@Rn / @-Rn
            return True
        if hi == 0x1 or (hi == 0x8 and ((op >> 8) & 15) in (0, 1)):  # mov.l Rm,@(d,Rn); mov.b/w R0,@(d,Rn)
            return True
        if hi == 0x0 and lo in (4, 5, 6):  # mov.x Rm,@(R0,Rn)
            return True
        if hi == 0xF and lo in (0xA, 0xB, 0x7):  # fmov para memoria
            return True
    return False


def has_call(t, b):
    for x in range(t, body_end(b), 2):
        op = w16(x)
        if (op >> 12) == 0x4 and (op & 0xFF) == 0x0B:  # jsr
            return True
        if (op >> 12) == 0xB or (op & 0xF0FF) == 0x0003:  # bsr, bsrf
            return True
    return False


# sem_progresso: voltas com estado repetido (BS_WW_VOLTAS, 4a coluna; -1 = nao medido). Mais de
# um back-edge pode ter o mesmo inicio: a contagem e do inicio (PC de retomada), comum a todos.
print('thread\tinicio\tbackedge\tbytes\tvoltas\tcom_store\tcom_chamada\tsem_progresso')
for (t, pc), (u, stuck) in sorted(prof.items(), key=lambda kv: -kv[1][0]):
    if u < minimo or pc not in loops:
        continue
    for b in loops[pc]:
        print(f'{t}\t{pc:08X}\t{b:08X}\t{b - pc + 2}\t{u}\t{"sim" if has_store(pc, b) else "NAO"}'
              f'\t{"sim" if has_call(pc, b) else "NAO"}\t{stuck}')
