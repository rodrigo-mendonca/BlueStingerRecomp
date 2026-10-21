"""N6-F: analise automatica de uma sessao gravada (tools/dev/gravacoes.py) para os criterios da fase F.

    python tools/dev/n6_f_sessao.py <sessao> [--refazer] [--saida <pasta>] [--legendas en-US]

1. Reproducao (gravacoes.py reproduzir): a sessao tem de ser DETERMINISTICA (estado por passo e saves).
2. A mesma entrada, headless ate o VBlank do rodape, com --audio nativo (log de audio, WAV da mistura, vozes da musica,
   estado) e --audio legado (estado); as saidas ficam em <saida> e so sao refeitas com --refazer.
3. Analise:
   - musicas: cada carga (mus-carga, pelo tamanho dos M*.MSB/MPB do disco) e cada tocar; "outra musica" = alguma
     sequencia tocada alem da M2A (o criterio aberto da F);
   - falas: para cada fala-start, o pedido de volume da musica no mesmo passo (ducking) e o pedido de volta no passo do
     estado 3 (fim natural); falas interrompidas sao listadas a parte;
   - legendas: cada linha do pacote (falas.<idioma>.toml dos mods do executavel) x o VBlank em que o jogo a mostrou,
     a partir do inicio do audio da fala (tolerancia 2 VBlanks);
   - estado nativo x legado: igual ate o 1o fim natural de fala nativa (a divergencia legitima: no legado a fala nao
     termina); depois, a posicao do Eliot e os saves gravados (conteudo, sem a data do envelope .bsv);
   - provedores sem falha ("sem camada" da musica nao e falha: notas com tecla fora de todas as camadas do programa, que
     o driver tambem nao toca, 0x0B78; docs/N6-PLANO.md 18.5); mistura: pico, saturadas e estouros do MIXS (informados; a saturacao e investigada na G).
Relatorio em <saida>/relatorio.md e .json. Codigo de saida 0 se os criterios da F passam (a saturacao nao reprova).
Nada muda o jogo: so as variaveis de diagnostico de sempre (BS_STATE_PASSO, BS_EFEITOS_LOG/WAV, BS_MUSICA_VOZES).
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tomllib
import wave
import array

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gravacoes as gv  # noqa: E402

HZ = 60000 / 1001


def rodar(c, sess, d, m, vb, legendas, log):
    """Executa a entrada da sessao com --audio m ate o VBlank vb; saidas <d>/<m>.*"""
    saves = os.path.join(d, m + ".saves")
    shutil.rmtree(saves, ignore_errors=True)
    shutil.copytree(os.path.join(gv.pasta(sess), "saves_inicio"), saves)
    env = {"BS_INPUT_SCRIPT": os.path.join(gv.pasta(sess), "entrada.txt"), "BS_STATE_PASSO": os.path.join(d, m + ".st.tsv"),
           "BS_STATE_PASSO_CADA": "1", "BS_EFEITOS_LOG": os.path.join(d, m + ".log.tsv")}
    if m == "nativo":
        env.update({"BS_EFEITOS_WAV": os.path.join(d, m + ".wav"), "BS_MUSICA_VOZES": os.path.join(d, m + ".vozes.tsv")})
    args = ["--saves-pasta", saves, "--audio", m, "--legendas", legendas, "--headless", str(vb)]
    log("  %s: --audio %s ate o VBlank %d" % (sess, m, vb))
    rc = gv.jogo(c, args, env, os.path.join(d, m))
    if rc != 0:
        raise SystemExit("execucao %s falhou (rc=%d); ver %s" % (m, rc, os.path.join(d, m + ".txt")))


def ler_log(p):
    out = []
    with open(p, encoding="utf-8", errors="replace") as f:
        next(f, None)
        for l in f:
            c = l.rstrip("\n").split("\t")
            if len(c) >= 4:
                out.append((int(c[0]), int(c[1]), c[2], c[3]))
    return out


def estado(p):
    out = {}
    with open(p, encoding="utf-8") as f:
        next(f, None)
        for l in f:
            c = l.strip().split("\t")
            if len(c) >= 7:
                out[int(c[0])] = c
    return out


def musicas_do_disco(data):
    """tamanho em bytes -> nomes (M*.MSB e M*.MPB do disco do usuario)"""
    d = os.path.join(data, "disc", "BSDATA")
    out = collections.defaultdict(list)
    for n in os.listdir(d):
        if re.fullmatch(r"M\w+\.(MSB|MPB)", n, re.I):
            out[os.path.getsize(os.path.join(d, n))].append(n.upper())
    return out


VERSAO_JOGO = "T13001N V1.000"  # a que o jogo passa a ModPackages::discover (app/game_host.cpp)
FORMATO_MOD = 1                 # kModFormat (native/localization/mod_packages.h)


def idioma_valido(tag):
    p = tag.replace("_", "-").split("-")
    if not tag or len(tag) > 35 or not p[0].isalpha() or not 2 <= len(p[0]) <= 3:
        return False
    i = 1
    if i < len(p) and len(p[i]) == 4 and p[i].isalpha():
        i += 1
    if i < len(p) and ((len(p[i]) == 2 and p[i].isalpha()) or (len(p[i]) == 3 and p[i].isdigit())):
        i += 1
    return i == len(p)


def idioma_canonico(tag):  # canonical_language (native/localization/locale.cpp)
    if not idioma_valido(tag):
        return ""
    out = []
    for k, x in enumerate(tag.replace("_", "-").split("-")):
        out.append(x.lower() if k == 0 else (x[0].upper() + x[1:].lower()) if len(x) == 4 else x.upper())
    return "-".join(out)


def recuo_idioma(lang):  # language_fallback: en-US -> en-US, en
    c, out = idioma_canonico(lang), []
    while c:
        out.append(c)
        c = c[:c.rfind("-")] if "-" in c else ""
    return out


def id_valido(i):  # valid_resource_id
    return (bool(i) and len(i) <= 128 and i[0] != "/" and i[-1] != "/" and ".." not in i and "//" not in i
            and all(ch.islower() or ch.isdigit() or ch in "._-:/" for ch in i))


def pacotes_validos(mods):
    """ModPackages::discover: pastas com mod.toml em ordem; formato 1..FORMATO_MOD, id valido e unico, 'jogo' igual a
    VERSAO_JOGO quando dado, 'idiomas' validos; ordem por prioridade (maior primeiro) e id. Devolve [(id, pasta)]."""
    out, ids = [], set()
    for n in sorted(os.listdir(mods)) if os.path.isdir(mods) else []:
        d = os.path.join(mods, n)
        mt = os.path.join(d, "mod.toml")
        if not (os.path.isdir(d) and os.path.isfile(mt)):
            continue
        try:
            with open(mt, "rb") as f:
                t = tomllib.load(f)
        except (tomllib.TOMLDecodeError, OSError):
            continue
        fmt = t.get("formato")
        if not isinstance(fmt, int) or not 1 <= fmt <= FORMATO_MOD:
            continue
        pid = t.get("id", "")
        if not isinstance(pid, str) or not id_valido(pid) or pid in ids:
            continue
        if "jogo" in t and t["jogo"] != VERSAO_JOGO:
            continue
        if any(not idioma_canonico(str(x)) for x in t.get("idiomas", [])):
            continue
        ids.add(pid)
        out.append((-int(t.get("prioridade", 0)), pid, d))
    out.sort()
    return [(pid, d) for _, pid, d in out]


def pacote_legendas(exe, lang):
    """O UNICO arquivo que o jogo usa (ModPackages::find + SubtitleService): o idioma exato em qualquer pacote vence o
    recuo; depois a prioridade. Devolve ("voz|cena" -> [(inicio, fim, texto)], arquivo)."""
    pacotes = pacotes_validos(os.path.join(os.path.dirname(exe), "mods"))
    for lg in recuo_idioma(lang):
        for _, d in pacotes:
            arq = os.path.join(d, "legendas", "falas.%s.toml" % lg)
            if os.path.isfile(arq):
                out = collections.defaultdict(list)
                with open(arq, "rb") as f:
                    for fl in tomllib.load(f).get("fala", []):
                        out[fl["voz"] + "|" + fl.get("cena", "")].append((float(fl["inicio"]), float(fl.get("fim", -1)), fl.get("texto", "")))
                for v in out.values():
                    v.sort()
                return out, arq
    return {}, None


def saturacao(wavp):
    if not os.path.exists(wavp):
        return {}
    w = wave.open(wavp, "rb")
    rate, n = w.getframerate(), w.getnframes()
    a = array.array("h")
    a.frombytes(w.readframes(n))
    clip = collections.Counter(int((i // 2) * 60000 / 1001 / rate) for i, x in enumerate(a) if x in (32767, -32768))
    grupos = []
    for v in sorted(clip):
        if grupos and v - grupos[-1][1] <= 30:
            grupos[-1][1] = v
            grupos[-1][2] += clip[v]
        else:
            grupos.append([v, v, clip[v]])
    pico = max((abs(x) for x in a), default=0)
    return {"pico": pico, "saturadas": sum(clip.values()), "trechos": grupos}


def analisar(sess, d, c, legendas):
    s = gv.ler_sessao(sess)
    r = {"sessao": sess, "origem": s["origem"], "criterios": {}}
    crit = r["criterios"]
    rep = json.load(open(os.path.join(gv.pasta(sess), "reproducao", "resultado.json"), encoding="utf-8"))
    crit["reproducao_deterministica"] = rep.get("veredito") == "DETERMINISTICA"
    log_n = ler_log(os.path.join(d, "nativo.log.tsv"))
    # musicas: o arquivo de cada slot pela carga (tamanho; as sequencias M*.MSB tem tamanhos unicos no disco) e cada
    # tocar pelo slot da sequencia pedido (banco)
    tam = musicas_do_disco(c["data"])
    slots = {}
    mus = []
    tocadas = []
    for p, vb, t, det in log_n:  # em ordem: cada tocar usa o slot carregado ate ali
        if t == "mus-tocar":
            m = re.search(r"banco=(-?\d+)", det)
            nomes = slots.get(int(m.group(1)), []) if m else []
            tocadas.append({"passo": p, "vblank": vb, "det": det, "musica": nomes[0].split(".")[0] if len(nomes) == 1 else None})
        if t == "mus-carga":
            m = re.search(r"tipo=(\w+) slot=(\d+) bytes=(\d+)", det)
            if m:
                ext = "MPB" if m.group(1) == "SMPB" else "MSB"
                nomes = [n for n in tam.get(int(m.group(3)), []) if n.endswith("." + ext)]
                mus.append({"passo": p, "vblank": vb, "tipo": m.group(1), "slot": int(m.group(2)), "bytes": int(m.group(3)), "arquivo": nomes})
                if ext == "MSB":
                    slots[int(m.group(2))] = nomes
    r["musicas"] = {"cargas": mus, "tocadas": tocadas}
    identificadas = [x["musica"] for x in tocadas]
    crit["musicas_identificadas"] = bool(tocadas) and None not in identificadas
    crit["outra_musica_alem_da_M2A"] = any(x and x != "M2A" for x in identificadas)
    # falas por episodio: do fala-start ate o proximo fala-start (o estado 3 e a interrupcao nao trazem o nome)
    inicios = [i for i, x in enumerate(log_n) if x[2] == "fala-start"]
    # os pedidos de volume BEM-SUCEDIDOS da porta da musica (0, r0 0), em ordem: base, descida e volta saem so daqui
    vol = []
    for x in log_n:
        m = re.search(r"porta=(\d+) v=(-?\d+) t=(\d+) r0=([0-9A-F]+)", x[3]) if x[2] == "mus-vol" else None
        if m and m.group(1) == "0" and int(m.group(4), 16) == 0:
            vol.append((x[0], int(m.group(2))))

    def volume_antes(passo):
        ant = [v for pp, v in vol if pp < passo]
        return ant[-1] if ant else None

    def pedidos(passo):  # neste passo ou no seguinte
        return [v for pp, v in vol if passo <= pp <= passo + 1]

    def base_e_descida(passo):
        """(volume antes da descida, volume da descida): o ultimo pedido da janela do inicio e a descida; a base e o
        pedido anterior a ela (na janela, ou antes do passo)."""
        seq = pedidos(passo)
        if not seq:
            return None, None
        desc = seq[-1]
        antes = seq[-2] if len(seq) >= 2 else volume_antes(passo)
        return antes, desc

    falas = []
    for k, i in enumerate(inicios):
        fim_ep = inicios[k + 1] if k + 1 < len(inicios) else len(log_n)
        ep = log_n[i:fim_ep]
        p, vb, _, det = log_n[i]
        nome = det.split("=", 1)[1]
        ini = next((x for x in ep if x[2] == "fala" and x[3] == "inicio " + nome), None)
        e3 = next((x for x in ep if x[2] == "fala-estado" and "(3)" in x[3]), None)
        inter = next((x for x in ep if x[2] == "fala" and x[3].startswith("interrompida")), None)
        antes, desc = base_e_descida(p)
        duck = pedidos(p)
        volta = pedidos(e3[0]) if e3 else []
        f = {"nome": nome, "passo": p, "vblank_inicio_audio": ini[1] if ini else None, "passo_estado3": e3[0] if e3 else None,
             "interrompida": inter is not None and (e3 is None or inter[0] < e3[0]), "volume_antes": antes, "ducking": duck, "volta": volta}
        # ducking: a musica baixa no inicio (volume menor que o de antes) e volta ao de antes no estado 3
        f["ducking_ok"] = antes is not None and desc is not None and desc < antes and e3 is not None and antes in volta
        falas.append(f)
    r["falas"] = falas
    naturais = [f for f in falas if f["passo_estado3"] and not f["interrompida"] and f["ducking"]]
    crit["fala_durante_musica_com_ducking"] = bool(naturais) and all(f["ducking_ok"] for f in naturais)
    # legendas: o arquivo que o jogo usa; cada linha esperada aparece (na ordem) a ate 2 VBlanks do tempo do pacote
    pac, arq = pacote_legendas(c["exe"], legendas)
    ev = []
    for l in open(os.path.join(d, "nativo.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"\[legenda\] VBlank (\d+): (\S+)", l)
        if m:
            ev.append((int(m.group(1)), m.group(2)))
    sinc, faltam, sobram = [], 0, 0
    for f in falas:
        linhas = pac.get(f["nome"] + "|", [])
        if f["vblank_inicio_audio"] is None or not linhas:
            continue
        ate = f["vblank_inicio_audio"] + (max(b for a, b, _ in linhas) + 1) * HZ
        mostradas = [v for v, n in ev if n == f["nome"] and f["vblank_inicio_audio"] <= v <= ate]
        esperadas = linhas  # fala interrompida: as linhas depois do corte nao contam como faltando
        faltam += max(0, len(esperadas) - len(mostradas)) if not f["interrompida"] else 0
        sobram += max(0, len(mostradas) - len(esperadas))
        for (a, b, txt), v in zip(esperadas, mostradas):
            sinc.append({"voz": f["nome"], "esperado": f["vblank_inicio_audio"] + a * HZ, "jogo": v, "texto": txt})
    erros = [x["jogo"] - x["esperado"] for x in sinc]
    r["legendas"] = {"arquivo": arq, "linhas": len(sinc), "faltando": faltam, "sobrando": sobram,
                     "erro_min_vb": min(erros, default=None), "erro_max_vb": max(erros, default=None)}
    crit["legendas_em_sincronia"] = bool(sinc) and faltam == 0 and sobram == 0 and all(-0.5 <= e <= 2.0 for e in erros)
    # estado nativo x legado
    en, el = estado(os.path.join(d, "nativo.st.tsv")), estado(os.path.join(d, "legado.st.tsv"))
    comuns = sorted(set(en) & set(el))
    dif = next((k for k in comuns if en[k][2:] != el[k][2:]), None)
    fim1 = min((f["passo_estado3"] for f in falas if f["passo_estado3"]), default=None)
    pos_dif = sum(1 for k in comuns if en[k][3:] != el[k][3:])
    saves_iguais = gv.hashes(os.path.join(d, "nativo.saves")) == gv.hashes(os.path.join(d, "legado.saves"))
    r["estado"] = {"passos_comuns": len(comuns), "primeira_diferenca": dif, "primeiro_fim_natural_de_fala": fim1,
                   "passos_com_vida_ou_posicao_diferente": pos_dif, "saves_nativo_legado_iguais": saves_iguais}
    alvo = fim1 if fim1 is not None else (max(comuns) if comuns else None)
    cobertura = bool(comuns) and alvo is not None and comuns[0] <= 1 and all(k in en and k in el for k in range(comuns[0], alvo + 1))
    r["estado"]["cobertura_ate"] = alvo
    crit["estado_igual_ao_legado_ate_o_fim_de_fala"] = cobertura and (dif is None or (fim1 is not None and dif > fim1))
    crit["saves_iguais_ao_legado"] = saves_iguais
    # provedores e mistura
    txt = open(os.path.join(d, "nativo.txt"), encoding="utf-8", errors="replace").read()
    linhas = {k: (re.findall(r"\[audio\] %s.*" % k, txt) or [""])[-1] for k in ("efeitos nativos", "musica nativa", "falas nativas", "mistura")}
    r["resumo_audio"] = linhas
    num = lambda l, k: int((re.search(k + r" (\d+)", l) or [0, -1])[1])
    mix = linhas["mistura"]
    crit["saida_sem_erro_nem_perda"] = num(mix, "escrita") == 0 and num(mix, "dispositivo") == 0 and num(mix, "descartes") in (0, -1)
    crit["provedores_sem_falha"] = (num(linhas["efeitos nativos"], "ausentes") == 0 and num(linhas["efeitos nativos"], "decodificacao") == 0
                                    and "(erros 0)" in linhas["musica nativa"] and num(linhas["musica nativa"], "recusadas") == 0
                                    and num(linhas["falas nativas"], "inesperadas") == 0 and num(linhas["falas nativas"], "leitura") == 0)
    r["mistura"] = saturacao(os.path.join(d, "nativo.wav"))
    r["mistura"]["mixs_estouros"] = num(linhas["mistura"], "20 bits")
    r["mistura"]["descartes"] = num(mix, "descartes")
    # o clipping introduzido pelo backend (uma fonte so acima de 16 bits); o de soma e o do modelo da AICA (G, 19.3)
    r["mistura"]["fonte_acima_16_bits"] = num(mix, "fonte acima de 16 bits")
    return r


def relatorio(r, d):
    L = ["# N6-F: sessao %s" % r["sessao"], ""]
    L += ["| Criterio | Resultado |", "|---|---|"] + ["| %s | %s |" % (k, "ok" if v else "**FALHA**") for k, v in r["criterios"].items()]
    L += ["", "## Musicas", ""] + ["- carga passo %d: %s slot %d, %d bytes = %s" % (m["passo"], m["tipo"], m["slot"], m["bytes"], ", ".join(m["arquivo"]) or "?") for m in r["musicas"]["cargas"]]
    L += ["- tocar passo %d (%s): %s" % (t["passo"], t["det"], t["musica"] or "NAO IDENTIFICADA") for t in r["musicas"]["tocadas"]]
    L += ["", "## Falas", ""] + ["- %s passo %d (audio no VBlank %s): musica em %s, pedidos no inicio %s; estado 3 no passo %s, pedidos %s; "
                                "ducking %s%s" % (f["nome"], f["passo"], f["vblank_inicio_audio"], f["volume_antes"], f["ducking"] or "-",
                                                  f["passo_estado3"], f["volta"] or "-", "ok" if f["ducking_ok"] else "nao",
                                                  " (interrompida)" if f["interrompida"] else "") for f in r["falas"]]
    lg = r["legendas"]
    L += ["", "## Legendas", "", "%s: %s linhas conferidas, %s faltando, %s sobrando; erro do inicio %s a %s VBlanks" % (
        lg["arquivo"], lg["linhas"], lg["faltando"], lg["sobrando"], lg["erro_min_vb"], lg["erro_max_vb"])]
    e = r["estado"]
    L += ["", "## Estado nativo x legado", "", "passos comuns %d; 1a diferenca %s; 1o fim natural de fala %s; vida/posicao diferente em %d "
          "passos; saves iguais: %s" % (e["passos_comuns"], e["primeira_diferenca"], e["primeiro_fim_natural_de_fala"],
                                        e["passos_com_vida_ou_posicao_diferente"], e["saves_nativo_legado_iguais"])]
    m = r["mistura"]
    L += ["", "## Mistura (informativo; a saturacao e da G)", "", "pico %s, saturadas (soma) %s, fonte acima de 16 bits %s, estouros do MIXS %s, blocos descartados na saida %s" % (m.get("pico"), m.get("saturadas"), m.get("fonte_acima_16_bits"), m.get("mixs_estouros"), m.get("descartes"))]
    L += ["- VBlank %d-%d: %d amostras" % tuple(g) for g in m.get("trechos", [])[:20]]
    L += ["", "## Resumo do audio nativo", ""] + ["    " + v for v in r["resumo_audio"].values()]
    open(os.path.join(d, "relatorio.md"), "w", encoding="utf-8").write("\n".join(L) + "\n")
    with open(os.path.join(d, "relatorio.json"), "w", encoding="utf-8") as f:
        json.dump(r, f, indent=2, ensure_ascii=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sessao")
    ap.add_argument("--saida")
    ap.add_argument("--refazer", action="store_true")
    ap.add_argument("--legendas", default="en-US")
    a = ap.parse_args()
    c = gv.config()
    d = a.saida or os.path.join(os.path.dirname(gv.RAIZ), "n6f", "sessoes", a.sessao)
    os.makedirs(d, exist_ok=True)
    rod = gv.rodape(os.path.join(gv.pasta(a.sessao), "entrada.txt"))
    if not rod:
        raise SystemExit("a sessao nao tem rodape (gravacao incompleta)")
    res = os.path.join(gv.pasta(a.sessao), "reproducao", "resultado.json")
    if a.refazer or not os.path.exists(res) or os.path.getmtime(res) < os.path.getmtime(os.path.join(gv.pasta(a.sessao), "entrada.txt")):
        gv.reproduzir(a.sessao)
    for m in ("nativo", "legado"):
        if a.refazer or not os.path.exists(os.path.join(d, m + ".st.tsv")):
            rodar(c, a.sessao, d, m, rod[1], a.legendas, print)
    r = analisar(a.sessao, d, c, a.legendas)
    relatorio(r, d)
    print(open(os.path.join(d, "relatorio.md"), encoding="utf-8").read())
    return 0 if all(r["criterios"].values()) else 1


if __name__ == "__main__":
    sys.exit(main())
