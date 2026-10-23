"""Sincronia das legendas com a voz realmente audivel (2026-10-23).

O ASR (faster-whisper) da o tempo das palavras, mas costuma esticar a primeira palavra de um segmento por cima do
silencio anterior (a legenda aparecia segundos antes da fala) e junta numa linha falas separadas por silencio (duas
falas consecutivas viravam uma legenda so). Aqui, para cada linha:
  - os trechos audiveis da propria fala (energia em janelas de 10 ms, 35 dB abaixo do pico, nunca abaixo de -50 dBFS;
    buracos menores que 150 ms fecham; trechos de pelo menos 50 ms) dizem onde a voz esta;
  - o inicio vai para o primeiro trecho audivel que a linha cobre (nunca antes do tempo do ASR) e o fim para o fim do
    ultimo trecho (mais FOLGA_FIM, sem invadir a linha seguinte);
  - uma linha que cobre trechos separados por PAUSA ou mais de silencio vira PARTES sob a mesma chave (recurso, linha):
    o texto e cortado onde a pausa cai, pela pontuacao das palavras do ASR ("frases": o numero de frases antes da pausa)
    ou, numa linha sem revisao (texto = o do ASR), na palavra exata ("palavras"). Sem um corte seguro, a linha fica
    inteira ("inteira") e e registrada: nada de texto inventado.
So a biblioteca padrao (wave, array, math).
"""
import array
import difflib
import math
import os
import re
import wave

JANELA_S, ABAIXO_PICO_DB, PISO_DB = 0.010, 35.0, -50.0
FECHA_BURACO_S, MIN_TRECHO_S = 0.150, 0.050
PAUSA, FOLGA_INI, FOLGA_FIM = 0.6, 0.05, 0.30
PAUSA_PALAVRA = 1.5
BORDA = 0.15  # sobreposicao minima para um trecho audivel pertencer a linha  # uma parte de uma palavra so com silencio vizinho a partir disto


def trechos_audiveis(wav_path, _cache={}):
    """[(inicio, fim)] em segundos dos trechos audiveis de um WAV PCM de 16 bits (as copias pcm/ do catalogo)."""
    if wav_path in _cache:
        return _cache[wav_path]
    if not os.path.exists(wav_path):
        _cache[wav_path] = None
        return None
    with wave.open(wav_path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{wav_path}: so PCM de 16 bits")
        ch, taxa, n = w.getnchannels(), w.getframerate(), w.getnframes()
        x = array.array("h", w.readframes(n))
    if ch > 1:
        x = array.array("h", x[::ch])
    jan = max(1, int(taxa * JANELA_S))
    dbs = []
    for i in range(0, len(x) - jan + 1, jan):
        s = sum(v * v for v in x[i:i + jan]) / jan
        dbs.append(10 * math.log10(s / (32768.0 * 32768.0) + 1e-12))
    if not dbs:
        _cache[wav_path] = []
        return []
    lim = max(max(dbs) - ABAIXO_PICO_DB, PISO_DB)
    on = [d > lim for d in dbs]
    fecha, minimo = int(FECHA_BURACO_S / JANELA_S), int(MIN_TRECHO_S / JANELA_S)
    segs, i = [], 0
    while i < len(on):
        if not on[i]:
            i += 1
            continue
        j = i
        while j < len(on) and (on[j] or any(on[j:j + fecha])):
            j += 1
        if j - i >= minimo:
            segs.append((round(i * JANELA_S, 3), round(j * JANELA_S, 3)))
        i = j
    _cache[wav_path] = segs
    return segs


def frases(texto):
    t = " ".join(texto.replace("\\n", " ").replace("\n", " ").split())
    return [f for f in re.split(r"(?<=[.!?])\s+", t) if f]


def _plano(texto):
    return " ".join(texto.replace("\\n", " ").replace("\n", " ").split())


def partes_da_linha(l, segs, prox_inicio, duracao, texto_asr_glossario=None):
    """Partes (inicio, fim, texto, corte) de uma linha de legenda. `l`: dict com inicio, fim, texto e, quando houver,
    palavras (as do ASR: dicts com i, f, p). corte: "inteira", "frases:<n>" (n frases antes da pausa) ou "palavras:<k>"."""
    ini, fim = float(l["inicio"]), float(l["fim"])
    texto = l["texto"]
    if not segs:
        return [(ini, fim, texto, "sem-audio")], "sem-audio"
    # so os trechos que a linha cobre de fato: um trecho que so encosta na borda (sobra da fala vizinha, menos de BORDA)
    # nao conta (a linha comecava na sobra da anterior e ficava antecipada)
    borda = min(BORDA, 0.3 * max(fim - ini, 0.0))
    dentro = [s for s in segs if min(s[1], fim) - max(s[0], ini) > borda or (s[0] >= ini and s[1] <= fim)]
    if not dentro:
        return [(ini, fim, texto, "inteira")], "sem-trecho"
    # grupos de trechos separados por PAUSA ou mais de silencio
    grupos = [[dentro[0]]]
    for s in dentro[1:]:
        if s[0] - grupos[-1][-1][1] >= PAUSA:
            grupos.append([s])
        else:
            grupos[-1].append(s)

    def janela(g, limite):
        a = max(ini, g[0][0] - FOLGA_INI)
        b = min(max(g[-1][1] + FOLGA_FIM, a + 0.5), limite, duracao)
        return round(a, 2), round(max(b, a + 0.1), 2)

    limite_linha = min(prox_inicio if prox_inicio is not None else duracao, duracao)
    if len(grupos) == 1:
        a, b = janela(grupos[0], limite_linha)
        return [(a, b, texto, "inteira")], "inteira"
    # onde cada pausa cai nas palavras do ASR: a ultima palavra que termina ate o comeco do silencio
    ws = l.get("palavras") or []
    cortes = []
    for g_ant, g_prox in zip(grupos, grupos[1:]):
        cand = [i for i, w in enumerate(ws) if w["f"] <= g_prox[0][0] + 0.10]
        cortes.append(min(cand, key=lambda i: abs(ws[i]["f"] - g_ant[-1][1])) if cand else None)
    plano = _plano(texto)
    tokens = plano.split(" ")
    pedacos, modo = None, "inteira"
    if ws and tokens and all(k is not None and k < len(ws) - 1 for k in cortes):
        # o texto pode ter sido revisado (pontuacao, nomes): alinha as palavras do ASR as do texto
        def norm(s):
            return re.sub(r"[^a-z0-9']", "", s.lower())
        sm = difflib.SequenceMatcher(a=[norm(w["p"]) for w in ws], b=[norm(x) for x in tokens], autojunk=False)
        mapa = {}
        for bl in sm.get_matching_blocks():
            for d in range(bl.size):
                mapa[bl.a + d] = bl.b + d
        if sm.ratio() >= 0.6:
            fins = [i for i, x in enumerate(tokens) if x[-1:] in ".!?"]
            js = []
            for k in cortes:
                antes = [mapa[i] for i in range(k, -1, -1) if i in mapa]
                if not antes:
                    js = None
                    break
                j = antes[0]
                perto = [b for b in fins if abs(b - j) <= 1 and b < len(tokens) - 1]
                js.append((min(perto, key=lambda b: abs(b - j)), True) if perto else (j, False))
            if js and all(j < len(tokens) - 1 for j, _ in js) and sorted(set(j for j, _ in js)) == [j for j, _ in js]:
                pedacos, a = [], 0
                for j, _ in js:
                    pedacos.append(" ".join(tokens[a:j + 1]))
                    a = j + 1
                pedacos.append(" ".join(tokens[a:]))
                if all(f for _, f in js):
                    ns = [sum(1 for b in fins if b <= j) for j, _ in js]
                    modo = "frases:" + ",".join(str(n) for n in ns) + "/" + str(len(frases(plano)))
                else:
                    modo = "palavras:" + ",".join(str(j + 1) for j, _ in js)
    if pedacos and len(pedacos) == len(grupos):
        # uma parte de uma palavra so fica separada se o silencio vizinho for longo (uma hesitacao de varios segundos depois
        # da primeira palavra); com pausa curta ela volta para a vizinha (nada de legenda picotada palavra a palavra)
        k = 0
        while k < len(pedacos) - 1:
            gap = grupos[k + 1][0][0] - grupos[k][-1][1]
            if gap < PAUSA_PALAVRA and (len(pedacos[k].split()) < 2 or len(pedacos[k + 1].split()) < 2):
                pedacos[k:k + 2] = [pedacos[k] + " " + pedacos[k + 1]]
                grupos[k:k + 2] = [grupos[k] + grupos[k + 1]]
            else:
                k += 1
        # o modo do corte a partir das partes finais (o pt-BR divide pelas frases)
        if all(x.rstrip()[-1:] in ".!?" for x in pedacos[:-1]):
            acum, ns = 0, []
            for x in pedacos[:-1]:
                acum += len(frases(x))
                ns.append(acum)
            modo = "frases:" + ",".join(str(n) for n in ns) + "/" + str(len(frases(plano)))
        else:
            acum, ks = 0, []
            for x in pedacos[:-1]:
                acum += len(x.split())
                ks.append(acum)
            modo = "palavras:" + ",".join(str(n) for n in ks)
        if len(pedacos) == 1:
            a, b = janela(grupos[0], limite_linha)
            return [(a, b, texto, "inteira")], "inteira"
    if not pedacos or any(not p for p in pedacos):
        a, b = janela(grupos[0], limite_linha)
        _, b = janela(grupos[-1], limite_linha)
        return [(a, b, texto, "inteira")], "pausa-sem-corte"
    out = []
    for gi, (g, p) in enumerate(zip(grupos, pedacos)):
        lim = grupos[gi + 1][0][0] if gi + 1 < len(grupos) else limite_linha
        a, b = janela(g, lim)
        out.append((a, b, p, modo))
    return out, modo


def dividir_traducao(texto_pt, corte, n_partes):
    """O texto pt-BR nas mesmas partes do ingles, so quando o corte e por frases e o numero total de frases e o mesmo
    do ingles (a frase n do pt traduz a frase n do en); senao None (a linha fica inteira, registrada)."""
    if not corte.startswith("frases:"):
        return None
    cortes, total = corte.split(":", 1)[1].split("/")
    ns = [int(x) for x in cortes.split(",")]
    fr = frases(_plano(texto_pt))
    if len(ns) + 1 != n_partes or len(fr) != int(total):
        return None
    out, a = [], 0
    for n in ns + [len(fr)]:
        out.append(" ".join(fr[a:n]))
        a = n
    return out if all(out) else None
