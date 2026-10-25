"""Testes do harness das referencias do N8.0 (tools/dev/n8_referencias.py): validacao e comparacao, sem o jogo.
Biblioteca padrao (unittest); cada caso monta pastas sinteticas com poucas linhas.

    python -m unittest tools/dev/test_n8_referencias.py -v
"""
import json
import os
import shutil
import sys
import tempfile
import unittest

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import n8_referencias as n8  # noqa: E402

CAB = {"estado": "passo\tvblank\testado\tvida\tx\ty\tz", "arquivos": "seq\tvblank\torigem\ttipo\tarquivo\tdeslocamento\tbytes\tsha256",
       "quadros": "vblank\tquadro\tentrada\tpasso", "efeitos": "passo\tvblank\ttipo\tdetalhe",
       "legendas": "vblank\tevento\tid\tocorrencia\tlinha\tatraso_saida_ms\ttexto", "filme_legendas": "logico_ms\taudivel_ms\tlegenda",
       "cartao": "passo\top\tresultado", "eventos": "# trace\nseq\tciclos\ttipo\ta\tb",
       "relogios": "vblank\ttmu_n\ttmu_h"}
FIM = 5
ROTA = {"tipo": "roteiro", "roteiro": "r.txt", "fim": FIM, "cartao": None, "tempo": "experimental", "vblanks": 100, "args": []}
SLOT1 = dict(ROTA, cartao="g1")
SESSAO = {"tipo": "sessao", "sessao": "sessao-x", "roteiro": "entrada.txt", "fim": FIM, "tempo": "nativo", "vblanks": 100, "args": []}
ESP_CGATE = n8.ESPERADO["passo_cgate"]
ESP_SLOT1 = n8.ESPERADO["passo_load_slot1"]
ESP_SESSAO = n8.ESPERADO["sessao-20261021-140145"]
H = {"bruto": "b" * 64, "normalizado": "n" * 64}


def eventos_padrao():
    # VBlank-In 2 e 4 (passo k no VBlank 2k); uma IRQ de nivel 6 (0x320) e uma troca de thread em cada intervalo
    out = []
    for vb in range(2, 2 * FIM + 3, 2):
        out += [("V", vb, 0), ("I", 0x320, 0x8C0010A0), ("T", 0, 1)]
    return out


class Caso:
    """Pasta de caso sintetica e valida para passo_cgate; os testes estragam uma parte."""

    def __init__(self, raiz, nome="c", estado=None, sem=(), vazios=(), eventos=None, cartao=None, info=None, saida="",
                 linhas_extra=None):
        self.p = os.path.join(raiz, nome)
        os.makedirs(self.p)
        passos = list(range(1, FIM + 1)) if estado is None else estado
        art = {
            "estado": ["%d\t%d\t%016X\t100\t0\t0\t0" % (s, 2 * s, s) for s in passos],
            "arquivos": ["%d\t%d\tnativo\tbsdata\tA.BIN\t0\t16\tab" % (i, i) for i in range(3)],
            "quadros": ["%d\t%016X\t0\t%d" % (2 * s, s, s) for s in range(0, FIM + 1)],
            "musica": ["10\t0.5\tvoz\t0\t40\t127\t1\tsa=1"],
            "efeitos": ["1\t2\ttocar\tid=1"],
            "legendas": ["-\tidioma\tpt-BR\t-\t-\t-\t", "4\tfala-pedido\tvoz/x\t-\t-\t-\tq"],
            "filme_legendas": ["10\t10\tx"],
            "cartao": cartao if cartao is not None else [],
            "eventos": ["%d\t%d\t%s\t%08X\t%08X" % (i, 1000 * i, t, a, b) for i, (t, a, b) in enumerate(eventos or eventos_padrao())],
            "relogios": ["%d\t1\t%016X" % (vb, vb) for vb in range(1, 2 * FIM + 3)],
        }
        for k, v in (linhas_extra or {}).items():
            art[k] = v
        for k, v in art.items():
            if k in sem:
                continue
            with open(os.path.join(self.p, k + ".tsv"), "w", encoding="utf-8", newline="\n") as f:
                if k in CAB:
                    f.write(CAB[k] + "\n")
                if k not in vazios:
                    f.write("".join(l + "\n" for l in v))
        i = {"rc": 0, "exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64, "ambiente": {"BS_RELOGIO_FIXO": "1700000000"},
             "saves": {}, "saves_inicio": {}, "saves_fim": {}}
        i.update(info or {})
        with open(os.path.join(self.p, "execucao.json"), "w", encoding="utf-8") as f:
            json.dump(i, f)
        with open(os.path.join(self.p, "jogo.txt"), "w", encoding="utf-8") as f:
            f.write(saida)


class Base(unittest.TestCase):
    def setUp(self):
        self.raiz = tempfile.mkdtemp(prefix="test_n8_")

    def tearDown(self):
        shutil.rmtree(self.raiz, ignore_errors=True)

    def caso(self, **kw):
        kw.setdefault("nome", "c%d" % len(os.listdir(self.raiz)))
        return Caso(self.raiz, **kw).p


class TestValidar(Base):
    def test_valido(self):
        self.assertEqual(n8.validar_caso(self.caso(), ROTA, ESP_CGATE), [])

    def test_u1_ausente(self):
        self.assertIn("musica ausente", n8.validar_caso(self.caso(sem=["musica"]), ROTA, ESP_CGATE))

    def test_u2_vazio(self):
        self.assertIn("efeitos vazio", n8.validar_caso(self.caso(vazios=["efeitos"]), ROTA, ESP_CGATE))

    def test_u2_legendas_so_com_o_idioma_e_vazio(self):
        p = self.caso(linhas_extra={"legendas": ["-\tidioma\tpt-BR\t-\t-\t-\t"]})
        self.assertIn("legendas vazio", n8.validar_caso(p, ROTA, ESP_CGATE))

    def test_u3_vazio_permitido(self):
        esp = n8.ESPERADO["passo_load_opcoes"]
        self.assertEqual(n8.validar_caso(self.caso(vazios=["musica"], sem=["filme_legendas"]), ROTA, esp), [])

    def test_u4_descontinuo(self):
        p = n8.validar_caso(self.caso(estado=[1, 2, 3, 5]), ROTA, ESP_CGATE)
        self.assertIn("estado: passo 4 ausente", p)

    def test_u4_repetido(self):
        self.assertIn("estado: passo 3 repetido", n8.validar_caso(self.caso(estado=[1, 2, 3, 3, 4, 5]), ROTA, ESP_CGATE))

    def test_u4_fora_de_ordem_ou_da_rota(self):
        # todos os passos uma vez, mas fora de ordem; passo 0; buraco depois do fim
        for est, ruim in (([2, 1, 3, 4, 5], 2), ([0, 1, 2, 3, 4, 5], 0), ([1, 2, 3, 4, 5, 7], 7)):
            p = n8.validar_caso(self.caso(estado=est), ROTA, ESP_CGATE)
            self.assertTrue(any(x.startswith("estado: passo %d na linha" % ruim) for x in p), (est, p))
        # o jogo segue depois do fim do roteiro: passos consecutivos alem do fim valem
        self.assertEqual(n8.validar_caso(self.caso(estado=list(range(1, FIM + 3))), ROTA, ESP_CGATE), [])
        self.assertIn("estado: passo 6 repetido", n8.validar_caso(self.caso(estado=[1, 2, 3, 4, 5, 6, 6]), ROTA, ESP_CGATE))

    def test_u5_truncado(self):
        # o defeito antigo: len(estado) >= fim - 1 aprovava
        self.assertIn("estado: passo 5 ausente", n8.validar_caso(self.caso(estado=[1, 2, 3, 4]), ROTA, ESP_CGATE))

    def test_u6_cartao_inicial_errado(self):
        info = {"saves_inicio": {"card0/BLUESTIN.000.bsv": H}, "saves_fim": {"card0/BLUESTIN.000.bsv": H}}
        orc = {"cartao": {"card0/BLUESTIN.000.bsv": "x" * 64}, "saves_fim": {"card0/BLUESTIN.000.bsv": H["normalizado"]}}
        p = n8.validar_caso(self.caso(info=info, cartao=["3\tload\tok", "4\tsave\tok"]), SESSAO, ESP_SESSAO, orc)
        self.assertIn("cartao inicial: card0/BLUESTIN.000.bsv diferente", p)

    def test_u6_rota_com_cartao(self):
        p = n8.validar_caso(self.caso(info={"saves_inicio": {"card0/X.bsv": H}}), ROTA, ESP_CGATE)
        self.assertTrue(any(x.startswith("cartao inicial: devia estar vazio") for x in p))

    def test_u7_formato_do_hash(self):
        info = {"saves_inicio": {"card0/BLUESTIN.000.bsv": H}, "saves_fim": {"card0/BLUESTIN.000.bsv": H}}
        cart = ["3\tload\tok", "4\tsave\tok"]
        for formato in ("bruto", "normalizado"):
            orc = {"cartao": {"card0/BLUESTIN.000.bsv": H[formato]}, "saves_fim": {"card0/BLUESTIN.000.bsv": H[formato]}}
            self.assertEqual(n8.validar_caso(self.caso(info=info, cartao=cart), SESSAO, ESP_SESSAO, orc), [], formato)

    def test_u7_bruto_de_outra_data_pelo_normalizado_conhecido(self):
        # saves_fim gravado no hash bruto (sessao 140145); o arquivo novo tem outra data: vale o normalizado conhecido
        novo = {"bruto": "c" * 64, "normalizado": H["normalizado"]}
        info = {"saves_inicio": {"card0/BLUESTIN.000.bsv": H}, "saves_fim": {"card0/BLUESTIN.000.bsv": novo}}
        orc = {"cartao": {"card0/BLUESTIN.000.bsv": H["bruto"]}, "saves_fim": {"card0/BLUESTIN.000.bsv": H["bruto"]},
               "bruto_norm": {H["bruto"]: H["normalizado"]}}
        self.assertEqual(n8.validar_caso(self.caso(info=info, cartao=["3\tload\tok", "4\tsave\tok"]), SESSAO, ESP_SESSAO, orc), [])
        orc["bruto_norm"] = {}
        p = n8.validar_caso(self.caso(info=info, cartao=["3\tload\tok", "4\tsave\tok"]), SESSAO, ESP_SESSAO, orc)
        self.assertIn("saves do fim: card0/BLUESTIN.000.bsv diferente", p)

    def test_u8_load_nao_executado(self):
        info = {"g1_sha256": n8.G1_SHA256, "importar_rc": 0, "saves_inicio": {"card0/BLUESTIN.000.bsv": H},
                "saves_fim": {"card0/BLUESTIN.000.bsv": H}}
        p = n8.validar_caso(self.caso(info=info, cartao=[]), SLOT1, ESP_SLOT1)
        self.assertTrue(any(x.startswith("cartao vazio") for x in p) and any(x.startswith("cartao: LOAD esperado") for x in p), p)
        self.assertEqual(n8.validar_caso(self.caso(info=info, cartao=["3\tload\tok"]), SLOT1, ESP_SLOT1), [])

    def test_u9_load_com_falha(self):
        info = {"g1_sha256": n8.G1_SHA256, "importar_rc": 0, "saves_inicio": {"card0/BLUESTIN.000.bsv": H},
                "saves_fim": {"card0/BLUESTIN.000.bsv": H}}
        p = n8.validar_caso(self.caso(info=info, cartao=["3\tload\tok", "5\tload\tfalha"]), SLOT1, ESP_SLOT1)
        self.assertIn("cartao: 1 load com falha", p)

    def test_u10_log_incompleto(self):
        ev = [e for e in eventos_padrao() if not (e[0] == "V" and e[1] > 6)]
        p = n8.validar_caso(self.caso(eventos=ev), ROTA, ESP_CGATE)
        self.assertTrue(any(x.startswith("eventos: log incompleto") for x in p), p)
        q = ["%d\t%016X\t0\t%d" % (2 * s, s, s) for s in range(0, FIM)]
        p = n8.validar_caso(self.caso(linhas_extra={"quadros": q}), ROTA, ESP_CGATE)
        self.assertTrue(any(x.startswith("quadros: log incompleto") for x in p), p)
        r = ["%d\t1\t%016X" % (vb, vb) for vb in range(1, 6)]
        p = n8.validar_caso(self.caso(linhas_extra={"relogios": r}), ROTA, ESP_CGATE)
        self.assertTrue(any(x.startswith("relogios: log incompleto") for x in p), p)

    def test_saida_com_falta_de_progresso(self):
        p = n8.validar_caso(self.caso(saida="erro: tempo experimental: falta de progresso: ..."), ROTA, ESP_CGATE)
        self.assertTrue(any(x.startswith("saida do jogo: erro: tempo experimental: falta de progresso") for x in p), p)

    def test_saida_com_contadores_normais(self):
        # linhas reais do [progresso]: contadores zerados e as faltas do modo sombra nao reprovam
        ok = ("[progresso] sombra: maior janela 9422257, faltas de progresso 2 (limite 64000000); decisoes continuar 0, "
              "avancar 0, deadlock 0\n[progresso] espera 8C064A2C: episodios 3726, sem evento 0, deadlock 0\n")
        self.assertEqual(n8.validar_caso(self.caso(saida=ok), ROTA, ESP_CGATE), [])
        p = n8.validar_caso(self.caso(saida=ok.replace("avancar 0, deadlock 0", "avancar 0, deadlock 3")), ROTA, ESP_CGATE)
        self.assertIn("saida do jogo: deadlock [1-9]", p)

    def test_u11_hash_inconsistente(self):
        man = {"exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64, "relogio": "1700000000"}
        ok = {"exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64, "ambiente": {"BS_RELOGIO_FIXO": "1700000000"}}
        self.assertEqual(n8.validar_configuracao(man, {"a": ok, "b": ok}), [])
        p = n8.validar_configuracao(man, {"a": ok, "b": dict(ok, exe_sha256="f" * 64)})
        self.assertIn("b: configuracao diferente (exe_sha256)", p)

    def test_u12_ambiente_diferente(self):
        man = {"exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64, "relogio": "1700000000"}
        ok = {"exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64,
              "ambiente": {"BS_RELOGIO_FIXO": "1700000000", "BS_INPUT_SCRIPT": "a.txt"}}
        outro = dict(ok, ambiente={"BS_RELOGIO_FIXO": "1700000001", "BS_INPUT_SCRIPT": "b.txt"})
        p = n8.validar_configuracao(man, {"a": ok, "b": outro})
        self.assertIn("b: configuracao diferente (BS_RELOGIO_FIXO 1700000001)", p)
        # caminhos diferentes por caso sao normais
        self.assertEqual(n8.validar_configuracao(man, {"a": ok, "b": dict(ok, ambiente=dict(ok["ambiente"], BS_INPUT_SCRIPT="c"))}), [])
        p = n8.validar_configuracao(man, {"a": ok, "b": dict(ok, ambiente=dict(ok["ambiente"], BS_RAM_MUTACAO="1:2"))})
        self.assertTrue(any("ambiente diferente" in x for x in p), p)


class TestComparar(Base):
    def cmp(self, r, x, esp=ESP_CGATE, exc=()):
        return n8.comparar(r, x, "passo_cgate_experimental", ROTA, esp, exc)

    def test_iguais(self):
        self.assertEqual(self.cmp(self.caso(), self.caso()), ([], []))

    def test_u13_diferenca_real(self):
        est = ["%d\t%d\t%016X\t100\t0\t0\t0" % (s, 2 * s, s if s != 3 else 99) for s in range(1, FIM + 1)]
        d, _ = self.cmp(self.caso(), self.caso(linhas_extra={"estado": est}))
        self.assertTrue(any(x.startswith("estado (") and "passo 3" in x for x in d), d)

    def test_u14_saves(self):
        d, _ = self.cmp(self.caso(), self.caso(info={"saves": {"card0/X.bsv": "1"}}))
        self.assertIn("saves", d)

    def test_u15_ausente_nos_dois_lados(self):
        d, _ = self.cmp(self.caso(sem=["musica"]), self.caso(sem=["musica"]))
        self.assertIn("musica ausente na referencia", d)
        self.assertIn("musica ausente na nova", d)

    def test_u16_vazio_nos_dois_lados(self):
        d, _ = self.cmp(self.caso(vazios=["efeitos"]), self.caso(vazios=["efeitos"]))
        self.assertIn("efeitos vazio na referencia", d)

    def test_u17_ordem(self):
        ev = eventos_padrao()
        ev[1], ev[2] = ev[2], ev[1]  # I e T trocados no primeiro intervalo
        d, _ = self.cmp(self.caso(), self.caso(eventos=ev))
        self.assertTrue(d and d[0].startswith("eventos: ") and "VBlank 2" in d[0], d)

    def test_u17b_entrega_atrasada_e_antecipada(self):
        ev = eventos_padrao()
        i = ev.pop(4)  # o I do intervalo do VBlank 4 vai para o do VBlank 6
        ev.insert(7, i)
        d, _ = self.cmp(self.caso(), self.caso(eventos=ev))
        self.assertTrue(d and "entrega atrasada" in d[0] and "VBlank 4" in d[0], d)
        ev = eventos_padrao()
        i = ev.pop(7)  # o I do VBlank 6 vai para o intervalo do VBlank 4
        ev.insert(5, i)
        d, _ = self.cmp(self.caso(), self.caso(eventos=ev))
        self.assertTrue(d and "VBlank 4" in d[0] and ("evento a mais" in d[0] or "ordem" in d[0]), d)

    def test_u18_coluna_de_tempo_ignorada(self):
        r = self.caso()
        x = self.caso()
        with open(os.path.join(x, "eventos.tsv"), encoding="utf-8") as f:
            ls = f.read().splitlines()
        with open(os.path.join(x, "eventos.tsv"), "w", encoding="utf-8", newline="\n") as f:
            for l in ls:
                c = l.split("\t")
                if len(c) == 5 and c[0].isdigit():
                    c[1] = str(int(c[1]) * 7 + 3)
                f.write("\t".join(c) + "\n")
        self.assertEqual(self.cmp(r, x), ([], []))

    def spc(self):
        ev = eventos_padrao()
        ev[4] = ("I", 0x320, 0x8C0010A2)  # intervalo do VBlank 4: o mesmo nivel, outro SPC
        return ev

    def test_u19_spc_diferente(self):
        d, _ = self.cmp(self.caso(), self.caso(eventos=self.spc()))
        self.assertTrue(d and "SPC 8C0010A0 x 8C0010A2" in d[0] and "VBlank 4" in d[0], d)

    def exc(self, **kw):
        e = {"caso": "passo_cgate_experimental", "vblank": 4, "intevt": "00000320", "spc_ref": "8C0010A0", "spc_novo": "8C0010A2",
             "justificativa": "j", "evidencia": "e"}
        e.update(kw)
        return [e]

    def test_u19b_spc_com_excecao(self):
        d, usadas = self.cmp(self.caso(), self.caso(eventos=self.spc()), exc=self.exc())
        self.assertEqual(d, [])
        self.assertEqual(len(usadas), 1)

    def test_u19c_excecao_que_nao_casa(self):
        for kw in ({"vblank": 6}, {"spc_novo": "8C0010A4"}, {"caso": "outro"}, {"intevt": "00000360"}):
            d, usadas = self.cmp(self.caso(), self.caso(eventos=self.spc()), exc=self.exc(**kw))
            self.assertTrue(d and "SPC" in d[0], (kw, d))
            self.assertEqual(usadas, [])

    def test_u19d_uma_excecao_vale_para_uma_diferenca(self):
        # duas entregas iguais no mesmo intervalo, as duas com o SPC trocado; uma entrada so libera uma
        ref = eventos_padrao()
        ref.insert(4, ("I", 0x320, 0x8C0010A0))
        nova = list(ref)
        nova[4] = nova[5] = ("I", 0x320, 0x8C0010A2)
        d, usadas = self.cmp(self.caso(eventos=ref), self.caso(eventos=nova), exc=self.exc())
        self.assertTrue(d and "SPC 8C0010A0 x 8C0010A2" in d[0], d)
        self.assertEqual(usadas, [])
        d, usadas = self.cmp(self.caso(eventos=ref), self.caso(eventos=nova), exc=self.exc() + self.exc())
        self.assertEqual((d, len(usadas)), ([], 2))

    def test_u19c_arquivo_de_excecoes_incompleto(self):
        p = os.path.join(self.raiz, "exc.toml")
        with open(p, "w", encoding="utf-8") as f:
            f.write('[[spc]]\ncaso = "a"\nvblank = 4\nintevt = "320"\nspc_ref = "8C0010A0"\nspc_novo = "8C0010A2"\n')
        with self.assertRaises(SystemExit):
            n8.carregar_excecoes(p)
        with open(p, "a", encoding="utf-8") as f:
            f.write('justificativa = "j"\nevidencia = "e"\n')
        self.assertEqual(n8.carregar_excecoes(p)[0]["intevt"], "00000320")

    def test_vblank_a_mais(self):
        ev = eventos_padrao() + [("V", 2 * FIM + 4, 0)]
        d, _ = self.cmp(self.caso(), self.caso(eventos=ev))
        self.assertTrue(d and d[0].startswith("eventos: VBlank-In diferentes"), d)

    def test_u20_caso_a_mais_ou_a_menos(self):
        self.assertEqual(n8.comparar_conjunto(["a", "b"], {"a": 1, "b": 1}), [])
        self.assertIn("caso b sem execucao nova", n8.comparar_conjunto(["a", "b"], {"a": 1}))
        self.assertIn("caso c fora do manifesto", n8.comparar_conjunto(["a"], {"a": 1, "c": 1}))


class TestMatriz(unittest.TestCase):
    def test_sessoes_sem_tempo_duplicado(self):
        self.assertEqual(n8.sem_tempo("--audio nativo --tempo nativo --x"), ["--audio", "nativo", "--x"])

    def test_cada_caso_tem_linha(self):
        for nome in ("passo_cgate_nativo", "passo_load_slot1_experimental", "sessao-20261021-145006_nativo"):
            self.assertIn(n8.base_do_caso(nome), n8.ESPERADO)

    def test_conjunto_completo_tem_16(self):
        self.assertEqual(len(n8.CASOS_N8), 16)
        self.assertEqual({n8.base_do_caso(n) for n in n8.CASOS_N8}, set(n8.ESPERADO))


class TestConjunto(Base):
    def validar(self, parcial):
        Caso(self.raiz, nome="passo_cgate_experimental")
        man = {"exe_sha256": "e" * 64, "disc_manifest_sha256": "d" * 64, "relogio": "1700000000",
               "casos": {"passo_cgate_experimental": ROTA}}
        if parcial is not None:
            man["parcial"] = parcial
        with open(os.path.join(self.raiz, "manifesto.json"), "w", encoding="utf-8") as f:
            json.dump(man, f)
        with open(os.devnull, "w") as nada:
            antigo, sys.stdout = sys.stdout, nada
            try:
                return n8.validar(n8.argparse.Namespace(ref=self.raiz))
            finally:
                sys.stdout = antigo

    def test_captura_completa_com_menos_casos_reprova(self):
        # sem a marca (inclusive manifestos anteriores a ela) ou com parcial=false: os 16 exatos
        self.assertEqual(self.validar(None), 1)

    def test_parcial_explicita(self):
        self.assertEqual(self.validar(True), 0)

    def test_parcial_falso_com_menos_casos(self):
        self.assertEqual(self.validar(False), 1)


if __name__ == "__main__":
    unittest.main()
