"""Testes de tools/dev/n8_nativo.py (S2n e S4n), sem o jogo: traces sinteticos com os defeitos que as conferencias
tem de reprovar (vazio, so cabecalho, truncado, incompleto, saida indevida, episodio aberto sem terminar nele).

    python tools/dev/test_n8_nativo.py
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import n8_nativo as n  # noqa: E402

N = 10


def saida(vb=N, ep=None, com=None, sem=0, dl=0, extra=""):
    ep = N - 1 if ep is None else ep
    com = ep - 1 if com is None else com
    return ("VBlanks: %d; quadro 640x476\n[progresso] espera 8C064A2C: episodios %d, voltas 5, avancos 3, saidas com "
            "evento %d, sem evento %d, deadlock %d; liberada por VBlankIn=%d; evento esperado VBlankIn: com %d, sem 0; "
            "maior episodio 1 ticks\n%s" % (vb, ep, com, sem, dl, com, com, extra))


def pg(n_vb=N, aberto_no_fim=True, lib=0, mask="7", fim_vb=None):
    """Um episodio da espera do VBlank por VBlank (1..n_vb-1), o ultimo aberto se `aberto_no_fim`."""
    ls = ["# monitor de progresso", n.CAB_PG]
    for v in range(1, n_vb):
        ls.append("quadro\t%d\t0\t-\t%d\t0\t1\t0\t-1" % (v, v * 100))
        ls.append("inicio\t%d\t0\t8C064A2C\t%d\t0\t0\t0\t-1" % (v, v * 100 + 1))
        if not (aberto_no_fim and v == n_vb - 1):
            ls.append("fim\t%d\t0\t8C064A2C\t%d\t%d\t9\t%s\t%d" % (v + 1 if fim_vb is None else fim_vb, v * 100 + 50,
                                                                  v * 100 + 1, mask, lib))
    ls.append("quadro\t%d\t0\t-\t%d\t0\t1\t0\t-1" % (n_vb, n_vb * 100))
    return ls


class TestS2n(unittest.TestCase):
    def ok(self, ls, txt):
        p, _ = n.s2n(ls, txt, N)
        self.assertEqual(p, [])

    def falha(self, ls, txt, trecho):
        p, _ = n.s2n(ls, txt, N)
        self.assertTrue(any(trecho in x for x in p), p)

    def test_aprovado_com_episodio_aberto_no_fim(self):
        self.ok(pg(), saida())

    def test_aprovado_sem_episodio_aberto(self):
        self.ok(pg(aberto_no_fim=False), saida(ep=N - 1, com=N - 1))

    def test_vazio(self):
        self.falha([], saida(), "sem cabecalho")

    def test_so_cabecalho(self):
        self.falha(["# monitor", n.CAB_PG], saida(), "nenhum episodio")

    def test_truncado_sem_fim_de_linha(self):
        ls = pg()
        ls[-3] = None
        self.falha(ls[:-2], saida(), "truncado")

    def test_trace_curto(self):
        self.falha(pg()[:-6], saida(), "ate o VBlank")

    def test_execucao_incompleta(self):
        self.falha(pg(), saida(vb=N - 2), "execucao incompleta")

    def test_erro_na_saida(self):
        self.falha(pg(), saida(extra="erro: tempo experimental: falta de progresso\n"), "erro na saida")

    def test_saida_sem_evento(self):
        self.falha(pg(lib=-1), saida(), "saidas sem evento")

    def test_saida_por_outro_evento(self):
        self.falha(pg(lib=2), saida(), "outro evento")

    def test_sem_vblank_in_na_mascara(self):
        self.falha(pg(mask="6"), saida(), "sem o evento esperado")

    def test_episodio_aberto_no_meio(self):
        ls = pg(aberto_no_fim=False)
        del ls[4]  # o fim do 1o episodio (cabecalho 2, quadro, inicio, fim)
        self.assertTrue(ls[3].startswith("inicio\t1\t") and ls[4].startswith("quadro\t2\t"))
        self.falha(ls, saida(), "inicio com episodio aberto")

    def test_aberto_cedo_sem_terminar_nele(self):
        # o ultimo episodio aberto comecou longe do fim
        ls = [x for x in pg(n_vb=N - 3) if not x.startswith("quadro\t%d\t" % (N - 3))]
        ls += ["quadro\t%d\t0\t-\t1\t0\t1\t0\t-1" % v for v in range(N - 3, N + 1)]
        self.falha(ls, saida(ep=N - 4, com=N - 5), "sem a execucao terminar dentro dele")

    def test_resumo_diferente(self):
        self.falha(pg(), saida(ep=N + 5), "resumo do monitor")

    def test_deadlock(self):
        self.falha(pg(), saida(dl=1), "deadlock")

    def test_sem_resumo(self):
        self.falha(pg(), "VBlanks: %d; quadro\n" % N, "sem o resumo")


def calls(n_vb=N, faltar=None, seq_buraco=False):
    ls = [n.CAB_CALLS]
    k = 0
    for v in range(0, n_vb):
        for fn in ("8C174888", "8C1586B8"):
            if faltar == (v, fn):
                continue
            s = k + 1 if seq_buraco and k == 5 else k
            ls.append("%d\t%s\t8C158840\t0\t0\t0\t0\t0\t8C15885E\t%d\t" % (s, fn, v))
            k += 1
    return ls


class TestS4n(unittest.TestCase):
    def roda(self, leg, som, sl=None, ss=None, neg=None, sn=None):
        return n.s4n(leg, som, sl or "VBlanks: %d;\n" % N, ss or "VBlanks: %d;\n" % N, N, neg, sn)[0]

    def test_iguais(self):
        self.assertEqual(self.roda(calls(), calls(), neg=calls(faltar=(3, "8C1586B8")), sn="VBlanks: %d;\n" % N), [])

    def test_diferenca_real(self):
        p = self.roda(calls(), calls(faltar=(4, "8C174888")))
        self.assertTrue(any("sequencia difere na chamada 8" in x for x in p), p)

    def test_vazio_dos_dois_lados(self):
        p = self.roda([n.CAB_CALLS], [n.CAB_CALLS])
        self.assertTrue(any("vazio" in x for x in p), p)

    def test_sem_arquivo(self):
        self.assertTrue(any("sem cabecalho" in x for x in self.roda([], [])))

    def test_truncado_igual_nos_dois(self):
        a = calls()[:-6]
        p = self.roda(a, list(a))
        self.assertTrue(any("ate o VBlank" in x for x in p), p)

    def test_linha_sem_fim(self):
        a = calls()
        a[-1] = None
        self.assertTrue(any("truncado" in x for x in self.roda(a, calls())))

    def test_seq_com_buraco(self):
        self.assertTrue(any("buraco" in x for x in self.roda(calls(seq_buraco=True), calls())))

    def test_execucao_incompleta(self):
        p = self.roda(calls(), calls(), ss="VBlanks: 4;\n")
        self.assertTrue(any("sombra: execucao incompleta" in x for x in p), p)

    def test_negativo_igual_reprova(self):
        p = self.roda(calls(), calls(), neg=calls(), sn="VBlanks: %d;\n" % N)
        self.assertTrue(any("nao detectado" in x for x in p), p)

    def test_negativo_truncado_nao_vazio_reprova(self):  # revisao Codex 2026-10-25
        p = self.roda(calls(), calls(), neg=calls(faltar=(3, "8C1586B8"))[:-6], sn="VBlanks: %d;\n" % N)
        self.assertTrue(any("negativo sem execucao" in x for x in p), p)

    def test_negativo_com_mais_chamadas_reprova(self):
        neg = calls() + ["%d\t8C1586B8\t8C158840\t0\t0\t0\t0\t0\t8C15885E\t%d\t" % (2 * N, N - 1)]
        p = self.roda(calls(), calls(), neg=neg, sn="VBlanks: %d;\n" % N)
        self.assertTrue(any("nao menos" in x for x in p), p)

    def test_negativo_sem_trace_reprova(self):
        p = self.roda(calls(), calls(), neg=[], sn="VBlanks: %d;\n" % N)
        self.assertTrue(any("negativo sem execucao" in x for x in p), p)


class TestLeitura(unittest.TestCase):
    def test_ultima_linha_sem_quebra_vira_none(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.tsv")
            with open(p, "w", newline="") as f:
                f.write("a\nb\nc")
            self.assertEqual(n.ler_linhas(p), ["a", "b", None])
            with open(p, "w", newline="") as f:
                f.write("a\r\nb\r\n")
            self.assertEqual(n.ler_linhas(p), ["a", "b"])
            self.assertEqual(n.ler_linhas(os.path.join(d, "nao")), [])


if __name__ == "__main__":
    unittest.main()
