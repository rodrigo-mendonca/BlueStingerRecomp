"""N5: legendas dos filmes (2026-10-23). Duas conferencias:

1. Pacotes (sem o jogo): em cada .srt de filme (en e pt-BR), contra as palavras do ASR do filme corrigidas
   (tools/dev/legendas/sincronia.palavras_corrigidas):
     - nenhuma linha comeca antes da voz dela (mais de 0,1 s antes da primeira palavra);
     - nenhuma linha fica mais de 1,5 s na tela depois da ultima palavra dela (o fim esticado ate a linha seguinte);
     - nenhuma linha cobre duas falas separadas por 1,5 s ou mais com um fim de frase entre elas (falas seguidas
       juntas); um corte no meio de uma frase nao e exigido (sem evidencia de outra fala).
2. Jogo (opcional, --log <BS_VIDEO_LEGENDA_LOG> --srt <o .srt usado>): cada linha entra uma vez so (sem piscar) e entra
   pela posicao AUDIVEL entre 0 e 0,2 s depois do inicio no .srt.

    python tools/dev/n5_legendas_filmes.py --pacotes <pasta com legendas-en e legendas-pt-br> [--trabalho <pasta>]
    python tools/dev/n5_legendas_filmes.py --log leg.tsv --srt NEWGAME.en.srt
"""
import argparse
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(AQUI, "legendas"))
import gerar_mod as gm  # noqa: E402
import sincronia  # noqa: E402

FILMES = ["NEWGAME", "E017", "E180", "E185A", "E193", "E195"]


def ler_srt(p):
    cues = []
    for bloco in open(p, encoding="utf-8").read().strip().split("\n\n"):
        ls = bloco.strip().split("\n")
        m = re.match(r"(\d+):(\d+):(\d+),(\d+) --> (\d+):(\d+):(\d+),(\d+)", ls[1]) if len(ls) >= 3 else None
        if m:
            g = [int(x) for x in m.groups()]
            cues.append((g[0] * 3600 + g[1] * 60 + g[2] + g[3] / 1000, g[4] * 3600 + g[5] * 60 + g[6] + g[7] / 1000, " ".join(ls[2:])))
    return cues


def pacotes(base, trabalho):
    falhas = 0
    for nome in FILMES:
        p = os.path.join(trabalho, "transcricoes", "filme-%s.json" % nome)
        ws = []
        for s in json.load(open(p, encoding="utf-8"))["segmentos"]:
            seg = [dict(w) for w in s["palavras"] if w["p"].strip()]
            if seg:
                seg[0]["inicio_segmento"] = True  # como em ressincronizar.palavras_asr
            ws += seg
        ws = sincronia.palavras_corrigidas(ws)
        for pasta, arq in (("legendas-en", "%s.en.srt"), ("legendas-pt-br", "%s.pt-br.srt")):
            cues = ler_srt(os.path.join(base, pasta, "video", arq % nome))
            erros = []
            for k, (a, b, txt) in enumerate(cues):
                proximo = cues[k + 1][0] if k + 1 < len(cues) else 1e9
                dentro = [w for w in ws if a - 0.1 <= (w["i"] + w["f"]) / 2 <= b + 0.05 and w["i"] < proximo]
                if not dentro:
                    continue  # sem palavras (musica, efeito): nada a medir
                if a < dentro[0]["i"] - 0.1:
                    erros.append("%.2f antecipada %.2f s: %s" % (a, dentro[0]["i"] - a, txt[:40]))
                if b > dentro[-1]["f"] + 1.5:
                    erros.append("%.2f fica %.1f s depois da voz: %s" % (a, b - dentro[-1]["f"], txt[:40]))
                for w0, w1 in zip(dentro, dentro[1:]):
                    if w1["i"] - w0["f"] >= 1.5 and w0["p"].strip()[-1:] in ".!?":
                        erros.append("%.2f junta falas separadas por %.1f s: %s" % (a, w1["i"] - w0["f"], txt[:40]))
            falhas += bool(erros)
            print("[%s] %s %s: %d linhas%s" % ("ok" if not erros else "FALHA", nome, pasta, len(cues),
                                               "".join("\n    " + e for e in erros)))
    return falhas


def jogo(log, srt):
    """As entradas do log, em ordem, contra as linhas do .srt, em ordem (nada de agrupar por texto: revisao Codex): cada
    entrada tem de ser a linha seguinte (repetir a mesma linha = piscar; pular = linha que nao apareceu), no audivel
    entre 0 e 0,2 s depois do inicio. Todas as linhas que comecam antes do ultimo ponto audivel do log sao exigidas."""
    cues = ler_srt(srt)
    linhas = [l.rstrip("\n").split("\t") for l in open(log, encoding="utf-8").readlines()[1:]]
    entradas = [(int(au) / 1000, txt) for _, au, txt in linhas if txt != "-"]
    ate = max((int(au) / 1000 for _, au, _ in linhas), default=0)
    erros, k = [], 0
    for au, txt in entradas:
        if k < len(cues) and txt == " ".join(cues[k][2].split()):
            a = cues[k][0]
            if not 0 <= au - a <= 0.2:
                erros.append("%.2f entra no audivel %.2f (%+.2f s): %s" % (a, au, au - a, txt[:40]))
            k += 1
        elif k > 0 and txt == " ".join(cues[k - 1][2].split()):
            erros.append("%.2f entra de novo no audivel %.2f (pisca): %s" % (cues[k - 1][0], au, txt[:40]))
        else:
            erros.append("entrada fora de ordem no audivel %.2f: %s" % (au, txt[:40]))
    faltam = [c for c in cues[k:] if c[0] < ate - 0.2]
    if faltam:
        erros.append("%d linha(s) antes de %.1f s nao apareceram (a primeira: %.2f %s)" % (len(faltam), ate, faltam[0][0], faltam[0][2][:30]))
    print("[%s] %s: %d linhas na tela ate %.1f s%s" % ("ok" if not erros and k else "FALHA", os.path.basename(log), k, ate,
                                                     "".join("\n    " + e for e in erros)))
    return int(bool(erros) or not k)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pacotes")
    ap.add_argument("--trabalho", default=gm.TRABALHO)
    ap.add_argument("--log")
    ap.add_argument("--srt")
    a = ap.parse_args()
    falhas = 0
    if a.pacotes:
        falhas += pacotes(a.pacotes, a.trabalho)
    if a.log:
        falhas += jogo(a.log, a.srt)
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
