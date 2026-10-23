"""Legendas EN (docs/LEGENDAS-EN-PLANO.md), passo 3: o pacote de mod a partir das transcricoes (rascunho para revisao).

Entrada (pasta de trabalho): catalogo.tsv, transcricoes/voz-<id>.json e filme-<NOME>.json (transcrever.py) e,
opcional, revisoes.tsv (correcoes humanas por voz e linha, que vencem o automatico). Saida: <trabalho>/mod/legendas-en,
no formato dos contratos do jogo (native/localization):
  mod.toml                          formato 1, id, nome, versao, prioridade, idiomas, jogo
  legendas/falas.en-US.toml         [[fala]] voz = "voz/<nome>", inicio, fim (s, desde o inicio da fala), texto
  legendas/<FILME>.en-US.srt        filmes do N5, no tempo de midia
  fontes/transcricoes.en-US.tsv     cada linha com confianca, estado, motivo e as correcoes do glossario
  fontes/glossario.tsv              os nomes e a fonte (texto do proprio jogo)
  README.md
e relatorio.md (cobertura). Nenhum audio ou video entra no pacote.

Regras (sem inventar texto):
  - glossario: so troca os nomes com fonte no texto do jogo (glossario.tsv), registrando cada troca;
  - confianca da linha = media da probabilidade das palavras; "revisar" se alguma palavra < 0,5 ou a media < 0,75;
    EXCLUIDA (fica pendente, nao vai para o jogo) se a media < 0,45 ou o trecho tiver menos de 2 palavras com
    probabilidade baixa (vocalizacao: "Ah!" num ambiente);
  - quebra: no maximo 2 linhas de 42 caracteres; frases longas divididas nas palavras (de preferencia na pontuacao),
    com o tempo das proprias palavras; duracao minima de 0,8 s sem invadir a linha seguinte nem passar do fim da fala.

    python tools/dev/legendas/gerar_mod.py [--trabalho <pasta>] [--versao 0.1.0]
"""
import argparse
import datetime
import glob
import json
import math
import re
import os
import shutil
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
TRABALHO = os.path.join(os.environ.get("LOCALAPPDATA", "."), "BlueStingerRecomp", "legendas-en")
MAX_LINHA, MAX_LINHAS, MIN_DUR, FOLGA = 42, 2, 0.8, 0.2
P_REVISAR, P_MEDIA_REVISAR, P_EXCLUIR = 0.5, 0.75, 0.45
IDIOMA = "en-US"


def ler_tsv(p):
    if not os.path.exists(p):
        return []
    ls = [l.rstrip("\n").split("\t") for l in open(p, encoding="utf-8") if l.strip()]
    return [dict(zip(ls[0], r)) for r in ls[1:]]


def glossario():
    g = ler_tsv(os.path.join(AQUI, "glossario.tsv"))
    return sorted(((r["errado"], r["certo"]) for r in g), key=lambda x: -len(x[0]))  # o mais longo primeiro


def aplicar_glossario(texto, gl):
    """Ate o texto parar de mudar: uma troca curta pode completar uma longa ("Elliot G. Balad" -> "Eliot G. Balad" ->
    "Eliot G. Ballade")."""
    trocas = []
    for _ in range(4):
        texto, t = _glossario_uma_vez(texto, gl)
        if not t:
            break
        trocas += t
    return texto, trocas


def _glossario_uma_vez(texto, gl):
    trocas = []
    for errado, certo in gl:
        if errado == certo:
            continue
        i = texto.find(errado)
        while i >= 0:
            fim = i + len(errado)
            # so a palavra inteira (nao troca "Elliott" dentro de outra palavra)
            if (i == 0 or not texto[i - 1].isalnum()) and (fim == len(texto) or not texto[fim].isalnum()):
                texto = texto[:i] + certo + texto[fim:]
                trocas.append(f"{errado}->{certo}")
                i = texto.find(errado, i + len(certo))
            else:
                i = texto.find(errado, fim)
    return texto, trocas


def quebrar(texto):
    """Ate 2 linhas de 42; None se nao couber (o chamador divide pelas palavras)."""
    if len(texto) <= MAX_LINHA:
        return texto
    if len(texto) > MAX_LINHA * MAX_LINHAS + 1:
        return None
    meio, melhor = len(texto) // 2, None
    for i, ch in enumerate(texto):
        if ch == " " and len(texto[:i]) <= MAX_LINHA and len(texto[i + 1:]) <= MAX_LINHA:
            if melhor is None or abs(i - meio) < abs(melhor - meio):
                melhor = i
    return None if melhor is None else texto[:melhor] + "\n" + texto[melhor + 1:]


def dividir(palavras):
    """Grupos de palavras que cabem em 2 linhas, cortando de preferencia depois de pontuacao."""
    grupos, atual = [], []
    for w in palavras:
        teste = "".join(x["p"] for x in atual + [w]).strip()
        if atual and quebrar(teste) is None:
            # volta ate a ultima pontuacao do grupo, se houver
            corte = max((k for k, x in enumerate(atual) if x["p"].rstrip()[-1:] in ".,!?;:"), default=len(atual) - 1)
            grupos.append(atual[:corte + 1])
            atual = atual[corte + 1:]
        atual.append(w)
    if atual:
        grupos.append(atual)
    return grupos


def linhas_de(transcricao, gl, duracao):
    """Linhas de legenda (inicio, fim, texto, confianca, estado, motivo, texto_asr, trocas) de uma transcricao."""
    out = []
    for s in transcricao["segmentos"]:
        ws = [w for w in s["palavras"] if w["p"].strip()]
        if not ws:
            continue
        for g in dividir(ws):
            asr = "".join(w["p"] for w in g).strip()
            probs = [w["prob"] for w in g]
            media = sum(probs) / len(probs)
            texto, trocas = aplicar_glossario(asr, gl)
            baixas = [w["p"].strip() for w in g if w["prob"] < P_REVISAR]
            # alucinacao tipica do modelo em sons que nao sao fala (um rosnado vira "GRRRR..." sem fim): letra repetida
            # 6 vezes ou mais, palavra que nao cabe numa linha, ou o indice de repeticao do proprio modelo
            if re.search(r"(\w)\1{5,}", asr) or any(len(w["p"].strip()) > MAX_LINHA for w in g) or s.get("compression_ratio", 0) > 2.4:
                out.append(dict(inicio=g[0]["i"], fim=g[-1]["f"], texto=asr[:80], confianca=media, estado="excluida",
                                motivo="provavel alucinacao (repeticao)", asr=asr[:200], trocas=""))
                continue
            if media < P_EXCLUIR or (len(g) < 2 and baixas):
                estado, motivo = "excluida", f"confianca {media:.2f}" + (f"; baixas {baixas}" if baixas else "")
            elif baixas or media < P_MEDIA_REVISAR:
                estado, motivo = "revisar", f"confianca {media:.2f}" + (f"; baixas {baixas}" if baixas else "")
            else:
                estado, motivo = "automatica", f"confianca {media:.2f}"
            out.append(dict(inicio=g[0]["i"], fim=g[-1]["f"], texto=quebrar(texto) or texto, confianca=media, estado=estado,
                            motivo=motivo, asr=asr, trocas=",".join(trocas)))
    # tempos: duracao minima e folga sem invadir a seguinte nem passar do fim
    for k, l in enumerate(out):
        prox = out[k + 1]["inicio"] if k + 1 < len(out) else duracao
        l["fim"] = min(max(l["fim"] + FOLGA, l["inicio"] + MIN_DUR), prox, duracao)
        l["fim"] = max(l["fim"], l["inicio"] + 0.1)
    return out


def aplicar_revisao(l, rv):
    """Revisao de uma linha (revisoes.tsv): texto e, opcionalmente, tempos e estado. tipo "humana" (padrao) vale como
    revisada; "ia" (revisao de texto por IA, sem ouvir o audio) fica "revisada-ia" e o pacote continua provisorio.
    estado "excluida" tira a linha do jogo (alucinacao do modelo em silencio); "revisar" mantem a marca com o texto novo."""
    tipo = rv.get("tipo") or "humana"
    if rv.get("texto"):
        t = rv["texto"].replace("\\n", "\n")
        l["texto"] = t if "\n" in t else (quebrar(t) or t)
    nota = rv.get("nota", "")
    if rv.get("estado") == "excluida":
        l["estado"], l["motivo"] = "excluida", f"revisao {tipo}: {nota or 'excluida'}"
    elif rv.get("estado") == "revisar":
        l["estado"], l["motivo"] = "revisar", f"revisao {tipo}, ainda duvidosa: {nota}"
    else:
        l["estado"] = "revisada" if tipo == "humana" else "revisada-ia"
        l["motivo"] = f"revisao {tipo}" + (f" ({rv.get('revisor')})" if rv.get("revisor") else "") + (f": {nota}" if nota else "")
    if rv.get("inicio"):
        l["inicio"], l["fim"] = float(rv["inicio"]), float(rv["fim"])


ESTADOS = {"automatica": "auto", "revisar": "revisar", "excluida": "excluidas", "revisada": "revisadas", "revisada-ia": "revisadas_ia"}


def t_inicio(t):  # 2 casas: o inicio arredonda para cima e o fim para baixo (nunca passa do fim da fala)
    return math.ceil(round(t * 100, 6)) / 100


def t_fim(t):
    return math.floor(round(t * 100, 6)) / 100


def toml_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def srt_tempo(t):
    ms = int(round(t * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabalho", default=TRABALHO)
    ap.add_argument("--versao", default="0.1.0")
    a = ap.parse_args()
    gl = glossario()
    cat = {r["id"]: r for r in ler_tsv(os.path.join(a.trabalho, "catalogo.tsv"))}
    # revisoes por recurso ("voz/<nome>" ou "filme/<nome>") e linha; a coluna antiga "voz" ainda vale
    # as do repositorio (revisoes.en-US.tsv, ao lado deste script) e, por cima, as locais da pasta de trabalho
    revis = {}
    for arq in (os.path.join(AQUI, f"revisoes.{IDIOMA}.tsv"), os.path.join(a.trabalho, "revisoes.tsv")):
        revis.update({(r.get("recurso") or r["voz"], int(r["linha"])): r for r in ler_tsv(arq)})
    mod = os.path.join(a.trabalho, "mod", "legendas-en")
    if os.path.isdir(mod):
        shutil.rmtree(mod)
    os.makedirs(os.path.join(mod, "legendas"))
    os.makedirs(os.path.join(mod, "fontes"))
    env_com_fala, sem_fala_nao_env = [], []  # a classificacao pela deteccao de voz (o nome ENV nao e prova)
    falas, tsv, rel = [], [], dict(vozes=0, com_fala=0, sem_fala=0, linhas=0, auto=0, revisar=0, excluidas=0, revisadas=0,
                                   revisadas_ia=0, filmes=0, linhas_filmes=0, bloqueadas=0, pendentes_transcricao=0)
    for vid, r in sorted(cat.items(), key=lambda x: int(x[0])):
        if r["nome"] == "-":
            continue
        rel["vozes"] += 1
        p = os.path.join(a.trabalho, "transcricoes", f"voz-{vid}.json")
        if r["transcricao"].startswith("bloqueada"):
            rel["bloqueadas"] += 1
            continue
        if not os.path.exists(p):
            rel["pendentes_transcricao"] += 1
            continue
        t = json.load(open(p, encoding="utf-8"))
        voz = "voz/" + os.path.splitext(r["nome"])[0].lower()
        ls = linhas_de(t, gl, float(r["segundos"]))
        for k, l in enumerate(ls):  # a revisao vence o automatico (texto, tempos, estado), antes de decidir se ha fala
            if (rv := revis.get((voz, k))):
                aplicar_revisao(l, rv)
        if not [l for l in ls if l["estado"] != "excluida"]:
            rel["sem_fala"] += 1
            if r["categoria"] != "ambiente":
                sem_fala_nao_env.append(r["nome"])
            for k, l in enumerate(ls):
                tsv.append([voz, vid, r["nome"], k, l["inicio"], l["fim"], l["texto"], l["asr"], f"{l['confianca']:.3f}", l["estado"], l["motivo"], l["trocas"], r["categoria"]])
            continue
        rel["com_fala"] += 1
        if r["categoria"] == "ambiente":
            env_com_fala.append(r["nome"])
        for k, l in enumerate(ls):
            rel["linhas"] += 1
            rel[ESTADOS[l["estado"]]] += 1
            tsv.append([voz, vid, r["nome"], k, l["inicio"], l["fim"], l["texto"], l["asr"], f"{l['confianca']:.3f}", l["estado"], l["motivo"], l["trocas"], r["categoria"]])
            if l["estado"] != "excluida":
                falas.append((voz, vid, k, l))
    with open(os.path.join(mod, "legendas", f"falas.{IDIOMA}.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"# Legendas em ingles das falas do Blue Stinger (pacote legendas-en {a.versao}).\n"
                "# Transcricao automatica (faster-whisper large-v3) com revisao de texto por IA, sem revisao humana: as linhas\n"
                "# marcadas \"revisar\" continuam duvidosas (fontes/transcricoes.en-US.tsv). Tempos em segundos desde o inicio\n"
                "# da fala.\n")
        for voz, vid, k, l in falas:
            o.write(f"\n[[fala]]  # id {vid}, linha {k}, {l['estado']}\nvoz = {toml_str(voz)}\ninicio = {t_inicio(l['inicio']):.2f}\n"
                    f"fim = {t_fim(l['fim']):.2f}\ntexto = {toml_str(l['texto'])}\n")
    for p in sorted(glob.glob(os.path.join(a.trabalho, "transcricoes", "filme-*.json"))):
        t = json.load(open(p, encoding="utf-8"))
        nome = t["chave"]
        todas = linhas_de(t, gl, t["duracao"])
        for k, l in enumerate(todas):
            if (rv := revis.get((f"filme/{nome.lower()}", k))):
                aplicar_revisao(l, rv)
        ls = [l for l in todas if l["estado"] != "excluida"]
        rel["filmes"] += 1
        for k, l in enumerate(todas):
            tsv.append([f"filme/{nome.lower()}", "-", nome, k, l["inicio"], l["fim"], l["texto"], l["asr"], f"{l['confianca']:.3f}", l["estado"], l["motivo"], l["trocas"], "filme"])
        if not ls:
            continue
        rel["linhas_filmes"] += len(ls)
        srt = "".join(f"{k}\n{srt_tempo(l['inicio'])} --> {srt_tempo(math.floor(l['fim'] * 1000) / 1000)}\n{l['texto']}\n\n"
                      for k, l in enumerate(ls, 1))
        # o mesmo SRT em dois lugares: o pacote (SubtitleService, desenho no N7) e o leiaute do N5 (mods\video\<NOME>.en.srt),
        # que o player de filmes ja desenha hoje (docs/MODS-VIDEO.md; ate a unificacao das pastas de mods)
        for destino in (os.path.join(mod, "legendas", f"{nome}.{IDIOMA}.srt"), os.path.join(mod, "video", f"{nome}.en.srt")):
            os.makedirs(os.path.dirname(destino), exist_ok=True)
            with open(destino, "w", encoding="utf-8", newline="\n") as o:
                o.write(srt)
    with open(os.path.join(mod, "fontes", f"transcricoes.{IDIOMA}.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("recurso\tid\tarquivo\tlinha\tinicio\tfim\ttexto\ttexto_asr\tconfianca\testado\tmotivo\tglossario\tcategoria\n")
        for row in tsv:
            o.write("\t".join(str(x).replace("\n", "\\n") if not isinstance(x, float) else f"{x:.2f}" for x in row) + "\n")
    shutil.copy(os.path.join(AQUI, "glossario.tsv"), os.path.join(mod, "fontes", "glossario.tsv"))
    with open(os.path.join(mod, "mod.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f'formato    = 1\nid         = "legendas-en"\nnome       = "Legendas em inglês (falas e filmes)"\n'
                f'versao     = "{a.versao}"\nprioridade = 10\nidiomas    = ["{IDIOMA}"]\njogo       = "T13001N V1.000"\n'
                # transcricao automatica sem revisao humana completa: o jogo avisa no log; nada a distribui
                f'provisorio = {"false" if rel["linhas"] and rel["revisadas"] == rel["linhas"] else "true"}\n')
    with open(os.path.join(mod, "README.md"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"""# Legendas em inglês para o Blue Stinger Native PC ({a.versao})

O Blue Stinger original não tem legendas. Este pacote acrescenta legendas em inglês às falas do jogo e aos filmes,
sem mudar o executável e sem conter áudio ou vídeo do jogo (só texto).

- `legendas/falas.{IDIOMA}.toml`: as falas do gameplay, por ID da fala (`voz/<nome>`), com tempos relativos ao início
  de cada fala (o jogo pode tocá-la a qualquer momento; a legenda acompanha o áudio).
- `legendas/<FILME>.{IDIOMA}.srt`: os filmes, no tempo de mídia.
- `fontes/transcricoes.{IDIOMA}.tsv`: cada linha com a confiança, o estado (`automatica`, `revisar`, `revisada`,
  `revisada-ia`, `excluida`) e as correções de nomes; base para a tradução para outros idiomas.

**Estado:** transcrição automática (faster-whisper large-v3, local) com revisão de texto por IA ({rel["revisadas_ia"]}
linhas corrigidas: nomes pelo texto do jogo, pontuação, alucinações do modelo em silêncio excluídas), ainda **não revisada
por humanos**. Linhas duvidosas continuam `revisar`; trechos incompreensíveis ficaram de fora (`excluida`).

**Uso:** é um **mod padrão** do projeto (`mods-padrao/`). O Builder (Recompiler, Developer e linha de comando) o
instala em toda instalação: a pasta vai para `mods\\legendas-en` e os `.srt` de `video\\` vão para `mods\\video`, ao lado do
`BlueStinger.exe`. As legendas aparecem nos filmes e nas falas do gameplay; o menu interno (Esc, Legendas) liga,
desliga e troca o idioma. Pela linha de comando: `--legendas {IDIOMA}|nenhuma`.
""")
    with open(os.path.join(a.trabalho, "relatorio.md"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"# Relatório da transcrição ({datetime.date.today().isoformat()})\n\n")
        o.write("| Medida | Valor |\n|---|---|\n")
        for k, v in rel.items():
            o.write(f"| {k} | {v} |\n")
        o.write("\n**Classificação pela detecção de voz** (o nome ENV não é prova; conferir de ouvido):\n\n")
        o.write(f"- nome ENV com fala detectada ({len(env_com_fala)}): {', '.join(env_com_fala) or '-'}\n")
        o.write(f"- sem ENV e sem fala detectada ({len(sem_fala_nao_env)}): {', '.join(sem_fala_nao_env) or '-'}\n")
    print(json.dumps(rel, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
