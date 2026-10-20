"""Legendas EN (docs/LEGENDAS-EN-PLANO.md), passo 2: transcricao automatica em ingles, local, com tempos por palavra.

Roda no venv isolado do ASR (%LOCALAPPDATA%\\BlueStingerRecomp\\tools\\asr, faster-whisper; MIT) sobre as copias PCM do
catalogo (passo 1) ou o audio extraido dos filmes. Cada arquivo vira um JSON com os segmentos, as palavras com tempo e
probabilidade, e as medidas de confianca do modelo (avg_logprob, no_speech_prob, compression_ratio). Nada e corrigido
aqui: a transcricao automatica e um rascunho para revisao (gerar_mod.py decide o que entra e o que fica pendente).

    <venv>\\Scripts\\python.exe tools/dev/legendas/transcrever.py [--saida <pasta>] [--ids 4,5,...] [--filmes]
        [--modelo large-v3] [--dispositivo auto|cuda|cpu]
Os modelos ficam em <saida>\\..\\tools\\asr-modelos (HF_HOME), fora do perfil do usuario.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

TRABALHO = os.path.join(os.environ.get("LOCALAPPDATA", "."), "BlueStingerRecomp")
os.environ.setdefault("HF_HOME", os.path.join(TRABALHO, "tools", "asr-modelos"))


def cuda_dlls():
    """As DLLs do cuBLAS/cuDNN das rodas nvidia-* do proprio venv (o ctranslate2 as procura no PATH)."""
    base = os.path.join(sys.prefix, "Lib", "site-packages", "nvidia")
    for d in glob.glob(os.path.join(base, "*", "bin")):
        os.add_dll_directory(d)
        os.environ["PATH"] = d + os.pathsep + os.environ["PATH"]


def carregar(modelo, dispositivo):
    from faster_whisper import WhisperModel
    tentativas = [("cuda", "float16"), ("cpu", "int8")] if dispositivo == "auto" else \
        [(dispositivo, "float16" if dispositivo == "cuda" else "int8")]
    erro = None
    for dev, tipo in tentativas:
        try:
            m = WhisperModel(modelo, device=dev, compute_type=tipo)
            return m, dev, tipo
        except Exception as e:  # noqa: BLE001 - registra e tenta o seguinte
            erro = e
            print(f"  {dev}/{tipo} indisponivel: {e}", file=sys.stderr)
    raise SystemExit(f"nenhum dispositivo para o modelo {modelo}: {erro}")


def transcrever(m, wav):
    t0 = time.time()
    segs, info = m.transcribe(wav, language="en", task="transcribe", word_timestamps=True, vad_filter=True,
                              vad_parameters={"min_silence_duration_ms": 300}, beam_size=5, temperature=0.0,
                              condition_on_previous_text=False)
    out = []
    for s in segs:
        out.append(dict(inicio=round(s.start, 3), fim=round(s.end, 3), texto=s.text.strip(),
                        avg_logprob=round(s.avg_logprob, 4), no_speech_prob=round(s.no_speech_prob, 4),
                        compression_ratio=round(s.compression_ratio, 3),
                        palavras=[dict(p=w.word, i=round(w.start, 3), f=round(w.end, 3), prob=round(w.probability, 4))
                                  for w in (s.words or [])]))
    return dict(duracao=round(info.duration, 3), fala_detectada=round(info.duration_after_vad, 3),
                idioma=info.language, prob_idioma=round(info.language_probability, 4), segmentos=out,
                segundos_de_processamento=round(time.time() - t0, 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--saida", default=os.path.join(TRABALHO, "legendas-en"))
    ap.add_argument("--ids", default="", help="so estes IDs do catalogo (virgula)")
    ap.add_argument("--filmes", default="", help="pasta com os MP4 do Builder (data\\video): transcreve o audio deles")
    ap.add_argument("--modelo", default="large-v3")
    ap.add_argument("--dispositivo", default="auto", choices=["auto", "cuda", "cpu"])
    a = ap.parse_args()
    cuda_dlls()
    m, dev, tipo = carregar(a.modelo, a.dispositivo)
    print(f"modelo {a.modelo} em {dev}/{tipo}")
    dest = os.path.join(a.saida, "transcricoes")
    os.makedirs(dest, exist_ok=True)
    tarefas = []
    if a.filmes:
        tmp = os.path.join(a.saida, "filmes-audio")
        os.makedirs(tmp, exist_ok=True)
        for mp4 in sorted(glob.glob(os.path.join(a.filmes, "*.mp4"))):
            nome = os.path.splitext(os.path.basename(mp4))[0]
            wav = os.path.join(tmp, nome + ".wav")  # copia temporaria so do audio; o filme nao e alterado
            if not os.path.exists(wav):
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", mp4, "-vn", "-ac", "1", "-ar", "16000", wav], check=True)
            tarefas.append(("filme", nome, wav))
    else:
        cat = [l.rstrip("\n").split("\t") for l in open(os.path.join(a.saida, "catalogo.tsv"), encoding="utf-8")]
        cols, rows = cat[0], [dict(zip(cat[0], r)) for r in cat[1:]]
        ids = {int(x) for x in a.ids.split(",") if x}
        for r in rows:
            if r["pcm"] and (not ids or int(r["id"]) in ids):
                tarefas.append(("voz", r["id"], os.path.join(a.saida, "pcm", r["pcm"])))
    for tipo_t, chave, wav in tarefas:
        alvo = os.path.join(dest, f"{tipo_t}-{chave}.json")
        if os.path.exists(alvo):
            continue
        r = transcrever(m, wav)
        r.update(tipo=tipo_t, chave=chave, arquivo=os.path.basename(wav), modelo=a.modelo, dispositivo=f"{dev}/{tipo}")
        json.dump(r, open(alvo, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print(f"{tipo_t} {chave}: {r['duracao']:.1f} s, fala {r['fala_detectada']:.1f} s, {len(r['segmentos'])} segmentos, "
              f"{r['segundos_de_processamento']:.1f} s de processamento")
    return 0


if __name__ == "__main__":
    sys.exit(main())
