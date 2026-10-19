"""Teste rapido do bu-trace sem Flycast (N4): roteiro, ancora, pulsos e bordas da entrada injetada, com um
GDB falso. python -I tools/dev/teste_bu_trace.py tools/dev tools/dev/roteiros_n4/painel_a.txt"""
import sys, struct
sys.path.insert(0, sys.argv[1])
import flycast_oracle as fo
segs = fo.roteiro_bu(sys.argv[2])
assert len(segs) == 2 and segs[1]["ancora"] == 0x0C01D200 and segs[1]["pulsos"] == (1 << 2, 90), segs
class G:
    def __init__(s): s.w = []
    def write(s, addr, data): s.w.append((addr, struct.unpack("<IIII", data)))
g = G(); e = fo.EntradaBu(segs)
ons = [e.aplica(g) for _ in range(1100)]
assert ons[0] == 8 and ons[3] == 0 and ons[1000] == 1 << 5 and ons[1002] == 0 and ons[1012] == 8, (ons[0], ons[3], ons[1000])
w0 = g.w[0]
assert w0[0] == 0x8C2D8970 + 8 and w0[1][0] == 8 and w0[1][2] == 8 and w0[1][3] == 0, w0
rel = [x for x in g.w if x[1][0] == 0 and x[1][3] == 8]
assert rel, "release de START"
assert e.ancora(0x0C01D200) and e.i == 1
a_on = [e.aplica(g) for _ in range(200)]
assert a_on[0] == 4 and a_on[2] == 0 and a_on[89] == 4 and a_on[92] == 0, a_on[:5]
print("entrada/roteiro/ancora/pulsos: ok;", len(g.w), "escritas")
