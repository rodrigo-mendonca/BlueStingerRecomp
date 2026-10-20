"""Testes do pipeline das legendas EN (docs/LEGENDAS-EN-PLANO.md). Biblioteca padrao (unittest), sem o jogo:
os casos com dados do usuario (o disco, o pacote gerado) sao pulados quando os dados nao existem.

    python -m unittest tools/dev/legendas/test_legendas.py -v
    BS_GAME_DATA=<pasta com BSDATA> BS_LEGENDAS=<pasta de trabalho> python -m unittest ...
"""
import os
import re
import struct
import sys
import tomllib
import unittest

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import catalogo  # noqa: E402
import gerar_mod  # noqa: E402

# as mesmas regras do native/localization/locale.cpp (valid_resource_id)
ID_VALIDO = re.compile(r"^(?!/)(?!.*//)(?!.*\.\.)[a-z0-9._:/-]{1,128}(?<!/)$")
SRT_TEMPO = re.compile(r"^(\d\d):(\d\d):(\d\d),(\d\d\d) --> (\d\d):(\d\d):(\d\d),(\d\d\d)$")


def fnv(samples):
    h = 0xcbf29ce484222325
    for v in samples:
        for b in struct.pack("<h", v):
            h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


class Decodificador(unittest.TestCase):
    def test_divisao_do_cpp(self):  # o C++ trunca para zero; o // do Python arredonda para baixo
        self.assertEqual(catalogo._trunc(-15, 8), -1)
        self.assertEqual(catalogo._trunc(15, 8), 1)

    def test_sintetico(self):
        # nibble 7 (+15/8 do passo) e 15 (-15/8): sinal e passo como no aica_adpcm.cpp
        pcm = catalogo.adpcm_aica(bytes([0xF7]), 2)
        self.assertEqual(pcm[0], 127 * 15 // 8)
        self.assertEqual(len(pcm), 2)

    @unittest.skipUnless(os.environ.get("BS_GAME_DATA"), "sem BS_GAME_DATA")
    def test_igual_ao_jogo(self):
        # os mesmos valores do teste C++ audio_voice_wav_decode_matches_pipeline (load_sound do jogo)
        for nome, n, h in (("E002A.WAV", 460464, 0xdf82023065f43bd5), ("E012BENV.WAV", 265214, 0x38b40368024de61a)):
            raw = open(os.path.join(os.environ["BS_GAME_DATA"], "BSDATA", nome), "rb").read()
            (fmt, data) = catalogo.wav_info(raw)
            pcm = catalogo.adpcm_aica(data, len(data) * 2)
            self.assertEqual(len(pcm), n)
            self.assertEqual(fnv(pcm), h, nome)


class Regras(unittest.TestCase):
    def test_glossario_so_palavra_inteira(self):
        gl = gerar_mod.glossario()
        t, trocas = gerar_mod.aplicar_glossario("This is Elliot Balad, EASER Troop.", gl)
        self.assertEqual(t, "This is Eliot Ballade, ESER Troop.")
        self.assertEqual(len(trocas), 2)
        t, _ = gerar_mod.aplicar_glossario("Elliotson and TEASER", gl)
        self.assertEqual(t, "Elliotson and TEASER")  # dentro de outra palavra: nao troca

    def test_quebra_em_duas_linhas(self):
        self.assertEqual(gerar_mod.quebrar("Curta."), "Curta.")
        q = gerar_mod.quebrar("The quick brown fox jumps over the lazy dog again, ok.")
        self.assertTrue(q and all(len(x) <= 42 for x in q.split("\n")) and q.count("\n") == 1)
        self.assertIsNone(gerar_mod.quebrar("x " * 60))

    def test_linhas_confianca_e_tempos(self):
        def w(p, i, f, prob):
            return dict(p=p, i=i, f=f, prob=prob)
        t = dict(segmentos=[
            dict(palavras=[w(" Hello,", 0.0, 0.3, 0.95), w(" there.", 0.35, 0.8, 0.97)]),
            dict(palavras=[w(" Ah!", 2.0, 2.1, 0.30)]),                                   # vocalizacao: excluida
            dict(palavras=[w(" Open", 3.0, 3.2, 0.9), w(" the", 3.25, 3.4, 0.4), w(" door.", 3.45, 3.9, 0.9)]),
        ])
        ls = gerar_mod.linhas_de(t, [], 4.0)
        self.assertEqual([l["estado"] for l in ls], ["automatica", "excluida", "revisar"])
        self.assertAlmostEqual(ls[0]["fim"], 1.0)               # +0,2 de folga
        self.assertLessEqual(ls[2]["fim"], 4.0)                  # nunca passa do fim da fala
        self.assertTrue(all(l["fim"] > l["inicio"] for l in ls))

    def test_alucinacao_excluida(self):  # um rosnado transcrito como "GRRRR..." nao vira legenda
        t = dict(segmentos=[dict(compression_ratio=1.0, palavras=[dict(p=" GRRRRRRRRRRRR", i=0.0, f=1.0, prob=0.9)]),
                            dict(compression_ratio=3.1, palavras=[dict(p=" no", i=1.0, f=1.2, prob=0.9), dict(p=" no", i=1.2, f=1.4, prob=0.9)])])
        self.assertEqual([l["estado"] for l in gerar_mod.linhas_de(t, [], 2.0)], ["excluida", "excluida"])


@unittest.skipUnless(os.path.isdir(os.path.join(os.environ.get("BS_LEGENDAS", ""), "mod", "legendas-en")), "sem o pacote gerado (BS_LEGENDAS)")
class Pacote(unittest.TestCase):
    """O pacote gerado segue os contratos do jogo (mod_packages, subtitle_service)."""

    @classmethod
    def setUpClass(cls):
        cls.trab = os.environ["BS_LEGENDAS"]
        cls.mod = os.path.join(cls.trab, "mod", "legendas-en")
        cls.cat = {r["nome"].upper(): r for r in gerar_mod.ler_tsv(os.path.join(cls.trab, "catalogo.tsv"))}

    def test_manifesto(self):
        m = tomllib.load(open(os.path.join(self.mod, "mod.toml"), "rb"))
        self.assertEqual(m["formato"], 1)
        self.assertRegex(m["id"], ID_VALIDO)
        self.assertEqual(m["idiomas"], ["en-US"])
        self.assertTrue(m["versao"] and m["nome"])

    def test_caminhos_seguros_e_sem_midia(self):
        for raiz, _, fs in os.walk(self.mod):
            for f in fs:
                rel = os.path.relpath(os.path.join(raiz, f), self.mod)
                self.assertNotIn("..", rel)
                self.assertFalse(f.lower().endswith((".wav", ".mp4", ".sfd", ".adx", ".flac", ".ogg")), rel)  # sem audio/video

    def test_falas(self):
        raw = open(os.path.join(self.mod, "legendas", "falas.en-US.toml"), "rb").read()
        raw.decode("utf-8")  # UTF-8 valido
        falas = tomllib.loads(raw.decode("utf-8"))["fala"]
        self.assertGreater(len(falas), 0)
        por_voz = {}
        for f in falas:
            self.assertRegex(f["voz"], ID_VALIDO)
            nome = f["voz"].split("/", 1)[1].upper() + ".WAV"
            self.assertIn(nome, self.cat, f["voz"])  # a voz existe no catalogo (ID original)
            dur = float(self.cat[nome]["segundos"])
            self.assertGreaterEqual(f["inicio"], 0)
            self.assertGreater(f["fim"], f["inicio"])
            self.assertLessEqual(f["fim"], dur + 1e-6, f["voz"])
            self.assertTrue(f["texto"].strip())
            self.assertTrue(all(len(x) <= 42 for x in f["texto"].split("\n")) and f["texto"].count("\n") <= 1, f["texto"])
            por_voz.setdefault(f["voz"], []).append((f["inicio"], f["fim"]))
        for voz, ts in por_voz.items():
            self.assertEqual(ts, sorted(ts), voz)
            for (a0, a1), (b0, _) in zip(ts, ts[1:]):
                self.assertLessEqual(a1, b0 + 1e-6, voz)  # sem sobreposicao na mesma fala

    def test_filmes_srt(self):
        for f in os.listdir(os.path.join(self.mod, "legendas")):
            if not f.endswith(".srt"):
                continue
            self.assertRegex(f, r"^[A-Z0-9_]+\.en-US\.srt$")
            blocos = open(os.path.join(self.mod, "legendas", f), encoding="utf-8").read().strip().split("\n\n")
            antes = -1
            for k, b in enumerate(blocos, 1):
                ls = b.split("\n")
                self.assertEqual(ls[0], str(k), f)
                m = SRT_TEMPO.match(ls[1])
                self.assertTrue(m, f"{f}: {ls[1]}")
                g = [int(x) for x in m.groups()]
                t0 = ((g[0] * 60 + g[1]) * 60 + g[2]) * 1000 + g[3]
                t1 = ((g[4] * 60 + g[5]) * 60 + g[6]) * 1000 + g[7]
                self.assertTrue(antes <= t0 < t1, f)
                antes = t1
                self.assertTrue(1 <= len(ls[2:]) <= 2 and all(len(x) <= 42 for x in ls[2:]), f)


if __name__ == "__main__":
    unittest.main()
