"""Legendas (docs/LEGENDAS-EN-PLANO.md), reconferencia: transcreve de novo as falas duvidosas com o vocabulario do jogo.

Mesmo modelo e mesmo venv do transcrever.py, mas com os nomes do glossario e do texto do jogo como `hotwords` e
`initial_prompt` (o Whisper erra nomes proprios sem contexto: "Eliot Balad", "Gene King"). O resultado vai para
<trabalho>/reconferencia/<tipo>-<chave>.json, ao lado da transcricao original (que nao muda). A escolha entre as duas
versoes e da revisao (revisoes.tsv): este passo so produz a alternativa com a confianca de cada palavra.

    <venv>\\Scripts\\python.exe tools/dev/legendas/reconferir.py [--trabalho <pasta>] --vozes E001C,E003,... [--filmes E017,...]
"""
import argparse
import json
import os
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import transcrever  # noqa: E402  (cuda_dlls, carregar, TRABALHO)

# Nomes com fonte: o glossario (texto do 1ST_READ.BIN) e os que o proprio audio confirma com confianca alta.
VOCABULARIO = ["Eliot G. Ballade", "ESER", "Dogs Bower", "Janine King", "Jean King", "Nephilim", "Kimra", "Kimratech",
               "Gigadent Disk", "Rat's Bar", "Chris Warus", "Hacking Defense Mode"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabalho", default=os.path.join(transcrever.TRABALHO, "legendas-en"))
    ap.add_argument("--vozes", default="", help="nomes dos WAV sem extensao (virgula)")
    ap.add_argument("--filmes", default="", help="nomes dos filmes (virgula)")
    ap.add_argument("--modelo", default="large-v3")
    a = ap.parse_args()
    transcrever.cuda_dlls()
    m, dev, tipo = transcrever.carregar(a.modelo, "auto")
    cat = [l.rstrip("\n").split("\t") for l in open(os.path.join(a.trabalho, "catalogo.tsv"), encoding="utf-8")]
    rows = {os.path.splitext(r[1])[0].upper(): dict(zip(cat[0], r)) for r in cat[1:]}
    dest = os.path.join(a.trabalho, "reconferencia")
    os.makedirs(dest, exist_ok=True)
    tarefas = [("voz", rows[v.upper()]["id"], os.path.join(a.trabalho, "pcm", rows[v.upper()]["pcm"]))
               for v in a.vozes.split(",") if v]
    tarefas += [("filme", f, os.path.join(a.trabalho, "filmes-audio", f + ".wav")) for f in a.filmes.split(",") if f]
    prompt = "Blue Stinger. " + ", ".join(VOCABULARIO) + "."
    for tipo_t, chave, wav in tarefas:
        segs, info = m.transcribe(wav, language="en", task="transcribe", word_timestamps=True, vad_filter=True,
                                  vad_parameters={"min_silence_duration_ms": 300}, beam_size=5, temperature=0.0,
                                  condition_on_previous_text=False, initial_prompt=prompt,
                                  hotwords=" ".join(VOCABULARIO))
        out = [dict(inicio=round(s.start, 3), fim=round(s.end, 3), texto=s.text.strip(), avg_logprob=round(s.avg_logprob, 4),
                    no_speech_prob=round(s.no_speech_prob, 4),
                    palavras=[dict(p=w.word, i=round(w.start, 3), f=round(w.end, 3), prob=round(w.probability, 4)) for w in (s.words or [])])
               for s in segs]
        r = dict(tipo=tipo_t, chave=chave, arquivo=os.path.basename(wav), modelo=a.modelo, dispositivo=f"{dev}/{tipo}",
                 prompt=prompt, duracao=round(info.duration, 3), segmentos=out)
        json.dump(r, open(os.path.join(dest, f"{tipo_t}-{chave}.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        for s in out:
            conf = sum(w["prob"] for w in s["palavras"]) / max(1, len(s["palavras"]))
            print(f"{tipo_t} {chave} {s['inicio']:.2f}-{s['fim']:.2f} ({conf:.2f}): {s['texto']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
