"""Testes de tools/dev/n8_excecoes_cadencia.py sem o jogo: pastas sinteticas (referencia e execucao nova) com uma espera
do hardware de duas entregas; a lista so sai se cada diferenca for provada, e os negativos abortam.

    python tools/dev/test_n8_excecoes_cadencia.py
"""
import os
import subprocess
import sys
import tempfile
import unittest

AQUI = os.path.dirname(os.path.abspath(__file__))
GERADOR = os.path.join(AQUI, "n8_excecoes_cadencia.py")
CAB_EV = "# trace\nseq\tciclos\ttipo\ta\tb\n"
CAB_INTC = "# intc\npos\tpasso\tvblank\tciclos\ttipo\tfonte\ta\tb\tistnrm\timl6nrm\timl4nrm\timl2nrm\tn\n"


def escrever(pasta, eventos, origens, estados=("1\t5\tAAAA\t100\t0\t0\t0",)):
    os.makedirs(pasta)
    with open(os.path.join(pasta, "eventos.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write(CAB_EV + "".join("%d\t0\t%s\t%s\t%s\n" % (i, t, a, b) for i, (t, a, b) in enumerate(eventos)))
    with open(os.path.join(pasta, "intc.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write(CAB_INTC + "".join("%d\t0\t0\t0\tE\t%s\t0\t0\t0\t0\t0\t0\t0\n" % (i, o) for i, o in origens.items()))
    with open(os.path.join(pasta, "quadros.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("vblank\tquadro\tentrada\tpasso\n" + "".join("%d\tX\t0\t0\n" % v for v in range(1, 6)))
    with open(os.path.join(pasta, "estado.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("passo\tvblank\testado\tvida\tx\ty\tz\n" + "".join(e + "\n" for e in estados))


# espera do hardware no VBlank 4: a primeira entrega ja com o retorno (8C15DE72), a segunda com o c->pc velho
REF = [("V", "00000004", "0"), ("I", "00000360", "8C15DE72"), ("R", "8C17DE4C", "0"), ("I", "00000320", "8C1983D2"),
       ("R", "8C17DE4C", "0"), ("V", "00000005", "0")]
NOVA = [REF[0], REF[1], REF[2], ("I", "00000320", "8C15DE72"), REF[4], REF[5]]
HW = {1: "hardware", 3: "hardware"}


class TestGerador(unittest.TestCase):
    def roda(self, ref, nova, origens=HW, estados_n=None):
        with tempfile.TemporaryDirectory() as d:
            escrever(os.path.join(d, "r", "caso_experimental"), ref, {})
            escrever(os.path.join(d, "n", "caso_experimental"), nova, origens,
                     *(() if estados_n is None else (estados_n,)))
            p = subprocess.run([sys.executable, GERADOR, os.path.join(d, "r"), os.path.join(d, "n"), "X"],
                               capture_output=True, text=True)
            return p.returncode, p.stdout, p.stderr

    def test_diferenca_provada_vira_uma_excecao(self):
        rc, out, err = self.roda(REF, NOVA)
        self.assertEqual(rc, 0, err)
        self.assertEqual(out.count("[[spc]]"), 1)
        self.assertIn('spc_novo = "8C15DE72"', out)

    def test_troca_de_thread_no_caminho_aborta(self):  # revisao Codex 2026-10-25
        ref = REF[:3] + [("T", "00000000", "00000001")] + REF[3:]
        nova = NOVA[:3] + [("T", "00000000", "00000001")] + NOVA[3:]
        self.assertNotEqual(self.roda(ref, nova, {1: "hardware", 4: "hardware"})[0], 0)

    def test_longjmp_e_thread_nova_no_caminho_abortam(self):
        for t in (("J", "8C0903F2", "00000001"), ("N", "00000003", "8C158840")):
            ref, nova = REF[:3] + [t] + REF[3:], NOVA[:3] + [t] + NOVA[3:]
            self.assertNotEqual(self.roda(ref, nova, {1: "hardware", 4: "hardware"})[0], 0, t)

    def test_entrega_fora_da_espera_do_hardware_aborta(self):
        self.assertNotEqual(self.roda(REF, NOVA, {1: "forcado", 3: "hardware"})[0], 0)
        self.assertNotEqual(self.roda(REF, NOVA, {1: "hardware", 3: "ldc_sr"})[0], 0)

    def test_spc_nao_provado_aborta(self):
        nova = NOVA[:3] + [("I", "00000320", "8C000000")] + NOVA[4:]
        self.assertNotEqual(self.roda(REF, nova)[0], 0)

    def test_evento_seguinte_diferente_aborta(self):
        nova = NOVA[:4] + [("R", "8C000000", "0")] + NOVA[5:]
        self.assertNotEqual(self.roda(REF, nova)[0], 0)

    def test_estado_seguinte_diferente_aborta(self):
        self.assertNotEqual(self.roda(REF, NOVA, estados_n=("1\t5\tBBBB\t100\t0\t0\t0",))[0], 0)


if __name__ == "__main__":
    unittest.main()
