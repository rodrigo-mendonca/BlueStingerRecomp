"""N8.0 (docs/N8-PLANO.md): referencias independentes do comportamento do produto ANTES dos cortes do N8, e a
conferencia de um executavel novo contra elas. Nada aqui muda o jogo: so roda o executavel com diagnosticos que ja
existem e guarda os artefatos (fora do Git: analysis/).

Matriz (headless, relogio fixo, provedores padrao do produto, filme nativo):
  - roteiros por passo de jogo: passo_newgame_area1, passo_cgate, passo_heliporto, passo_combate (NEW GAME, filme,
    troca de sala, combate, GAME OVER), passo_load_opcoes (LOAD sem cartao, OPTIONS) e passo_load_slot1 (LOAD real do
    save g1 importado num cartao temporario);
  - as sessoes gravadas do mantenedor (tools/dev/gravacoes.py): SAVE, LOAD, troca de sala, combate, musica;
  - cada uma no tempo estavel (--tempo nativo) e no experimental.
Artefatos por execucao: estado por passo, leituras de arquivo, quadros (hash por VBlank, com o passo), vozes da musica,
efeitos, legendas das falas e dos filmes, operacoes do cartao, ordem de eventos (BS_EVENT_TRACE: VBlank, interrupcao
com nivel e SPC, threads), leituras de relogio por VBlank (BS_CLOCK_LOG), cartao no inicio e no fim, quadro final e a
saida do jogo; mais o manifesto (hashes do executavel e dos dados, commit, argumentos, ambiente, orcamento).

    python tools/dev/n8_referencias.py capturar --exe <exe> --data <data> --saida <pasta> [--casos a,b] [--paralelo 4]
    python tools/dev/n8_referencias.py conferir --exe <exe> --ref <pasta> [--data <data>] [--casos a,b] [--paralelo 4]
                                               [--ambiente K=V ...] [--excecoes <arquivo.toml>]
                                               (negativo: --ambiente BS_RAM_MUTACAO=3000:8C24FBCC; "{pasta}" no
                                               valor vira a pasta do caso: --ambiente BS_INTC_TRACE={pasta}/intc.tsv)
    python tools/dev/n8_referencias.py validar --ref <pasta>     (so a validacao dos artefatos, sem rodar o jogo)

Validacao (`validar_caso`, docs/N8-PLANO.md, especificacao do harness): cada caso tem a sua tabela de artefatos
obrigatorios (ESPERADO). Estado contiguo de 1 ao ultimo passo do roteiro; artefato obrigatorio ausente ou vazio
reprova; LOAD/SAVE exigidos pelo cartao.tsv; cartao inicial e saves do fim conferidos (sessoes: contra o saves_fim do
sessao.json, no hash bruto ou no normalizado); configuracao unica (o mesmo executavel, dados e ambiente em todos).

Comparacao (`comparar`) por PASSO de jogo (o VBlank pode mudar com o tempo sem ciclos do N8.1): estado por passo e
saves iguais (estrito), leituras na mesma ordem e conteudo, quadros por passo, sequencias de vozes, efeitos, legendas,
operacoes do cartao sem a coluna de tempo, relogios por VBlank e eventos por intervalo entre dois VBlank-In: a sequencia
ordenada I(nivel, SPC)/T/N/R/J, estrita. Uma diferenca de SPC so passa com uma entrada explicita no arquivo de excecoes
(caso, VBlank, os dois SPC, justificativa, evidencia), e nunca entre execucoes do mesmo executavel. Sai com 1 se algo
diferir.
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib

AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(AQUI))
sys.path.insert(0, AQUI)
import gravacoes as gv  # noqa: E402

ROTEIROS = os.path.join(REPO, "tools", "dist", "roteiros")
G1 = os.path.join(os.environ.get("LOCALAPPDATA", ""), "BlueStingerRecomp", "n6f", "jogo", "g1", "BLUESTIN.000")
G1_SHA256 = "1644836c04cc450f85b8e0a0fff2d4dcc0137d1cb4ea41ea91d3f04820f3f415"  # o .000 de origem das sessoes (sessao.json)
FILME_NEWGAME = 28000  # VBlanks do filme NEWGAME nativo (452 s), que toca dentro de um passo de jogo
LOGS = {"estado": "BS_STATE_PASSO", "arquivos": "BS_FILE_LOG", "quadros": "BS_FRAME_LOG", "musica": "BS_MUSICA_VOZES",
        "efeitos": "BS_EFEITOS_LOG", "legendas": "BS_LEGENDA_LOG", "filme_legendas": "BS_VIDEO_LEGENDA_LOG",
        "cartao": "BS_SAVES_LOG", "eventos": "BS_EVENT_TRACE", "relogios": "BS_CLOCK_LOG"}

# ---- matriz dos casos: artefatos obrigatorios (O) ou que podem ficar vazios/ausentes (V) ----
# Mesma tabela nos dois modos de tempo. "cartao": None = sem exigencia (V); senao o minimo de "load ok"/"save ok" e
# nenhuma "falha". "saves_fim": "vazio" (a rota nao grava), "inicio" (= o cartao inicial) ou "sessao" (= saves_fim do
# sessao.json).
_O, _V = "O", "V"
_BASE = {"estado": _O, "arquivos": _O, "quadros": _O, "efeitos": _O, "eventos": _O, "relogios": _O, "cartao": _V}
ESPERADO = {
    "passo_newgame_area1": dict(_BASE, musica=_V, legendas=_O, filme_legendas=_O, cartao_ops=None, saves_fim="vazio"),
    "passo_cgate":         dict(_BASE, musica=_O, legendas=_O, filme_legendas=_O, cartao_ops=None, saves_fim="vazio"),
    "passo_heliporto":     dict(_BASE, musica=_O, legendas=_O, filme_legendas=_O, cartao_ops=None, saves_fim="vazio"),
    "passo_combate":       dict(_BASE, musica=_O, legendas=_O, filme_legendas=_O, cartao_ops=None, saves_fim="vazio"),
    "passo_load_opcoes":   dict(_BASE, musica=_V, legendas=_V, filme_legendas=_V, cartao_ops=None, saves_fim="vazio"),
    "passo_load_slot1":    dict(_BASE, musica=_O, legendas=_V, filme_legendas=_V, cartao=_O,
                                cartao_ops={"load": 1}, saves_fim="inicio"),
    "sessao-20261021-140145": dict(_BASE, musica=_O, legendas=_V, filme_legendas=_V, cartao=_O,
                                   cartao_ops={"load": 1, "save": 1}, saves_fim="sessao"),
    "sessao-20261021-145006": dict(_BASE, musica=_O, legendas=_V, filme_legendas=_V, cartao=_O,
                                   cartao_ops={"load": 8, "save": 1}, saves_fim="sessao"),
}
# variaveis do ambiente que sao caminhos (diferem por caso); as outras BS_* tem de ser iguais em todos os casos
_CAMINHOS = set(LOGS.values()) | {"BS_INPUT_SCRIPT", "BS_INTC_TRACE"}


def base_do_caso(nome):
    return re.sub(r"_(nativo|experimental)$", "", nome)


# o conjunto completo: cada caso da ESPERADO nos dois modos de tempo (16). Uma captura sem --casos tem de ter todos;
# com --casos o manifesto fica marcado como parcial
CASOS_N8 = frozenset("%s_%s" % (b, t) for b in ESPERADO for t in ("nativo", "experimental"))


def ultimo_passo(roteiro):
    ult = 0
    for l in open(roteiro, encoding="utf-8"):
        t = l.split("#")[0].strip()
        m = re.match(r"^(\d+)\s", t)
        if m:
            ult = int(m.group(1))
    return ult


def sem_tempo(args):
    """Os argumentos de uma sessao sem --tempo: o modo vem do caso (nativo = o da gravacao, anterior a 2026-10-24)."""
    a, out = args.split(), []
    i = 0
    while i < len(a):
        if a[i] == "--tempo":
            i += 2
            continue
        out.append(a[i])
        i += 1
    return out


def casos():
    out = {}
    for nome, filme, cartao in (("passo_newgame_area1", True, None), ("passo_cgate", True, None), ("passo_heliporto", True, None),
                                ("passo_combate", True, None), ("passo_load_opcoes", False, None), ("passo_load_slot1", False, "g1")):
        r = os.path.join(ROTEIROS, nome + ".txt")
        fim = ultimo_passo(r)
        for tempo in ("nativo", "experimental"):
            out["%s_%s" % (nome, tempo)] = {"tipo": "roteiro", "roteiro": r, "fim": fim, "cartao": cartao, "tempo": tempo,
                                            "vblanks": fim * 2 + 3000 + (FILME_NEWGAME if filme else 0), "args": []}
    for s in sorted(os.listdir(gv.RAIZ)) if os.path.isdir(gv.RAIZ) else []:
        if s not in ESPERADO or not os.path.exists(os.path.join(gv.RAIZ, s, "sessao.json")):
            continue
        d = gv.pasta(s)
        rod = gv.rodape(os.path.join(d, "entrada.txt"))
        if not rod:
            continue
        fim, vb = rod
        for tempo in ("nativo", "experimental"):
            out["%s_%s" % (s, tempo)] = {"tipo": "sessao", "sessao": s, "roteiro": os.path.join(d, "entrada.txt"), "fim": fim,
                                         "tempo": tempo, "vblanks": vb + 600 + (vb // 2 if tempo == "nativo" else 0),
                                         "args": sem_tempo(gv.ler_sessao(s)["args"])}
    return out


def sha(path, bloco=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(bloco), b""):
            h.update(b)
    return h.hexdigest()


def cartao_hashes(d):
    """Cada arquivo do cartao (caminho relativo) -> {"bruto": sha256 do arquivo, "normalizado": sem a data do .bsv}."""
    norm = gv.hashes(d) if os.path.isdir(d) else {}
    return {rel: {"bruto": sha(os.path.join(d, rel)), "normalizado": h} for rel, h in norm.items()}


def bruto_para_normalizado():
    """bruto -> normalizado dos cartoes iniciais de todas as sessoes gravadas (arquivos reais): o sessao.json da
    sessao-20261021-140145 guardou os saves do fim no hash bruto; a sessao seguinte comecou desses mesmos arquivos."""
    m = {}
    for s in sorted(os.listdir(gv.RAIZ)) if os.path.isdir(gv.RAIZ) else []:
        for h in cartao_hashes(os.path.join(gv.RAIZ, s, "saves_inicio")).values():
            m[h["bruto"]] = h["normalizado"]
    return m


def git_commit():
    env = dict(os.environ, GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="safe.directory", GIT_CONFIG_VALUE_0="*")
    c = subprocess.run(["git", "-C", REPO, "describe", "--always", "--dirty", "--abbrev=12"], capture_output=True, text=True, env=env)
    return c.stdout.strip() or "desconhecido"


def preparar_cartao(exe, data, caso, pasta):
    dst = os.path.join(pasta, "saves")
    info = {}
    if caso["tipo"] == "sessao":
        shutil.copytree(os.path.join(gv.pasta(caso["sessao"]), "saves_inicio"), dst)
    else:
        os.makedirs(dst)
        if caso.get("cartao") == "g1":
            if not os.path.exists(G1):
                raise SystemExit("save g1 ausente (%s)" % G1)
            info["g1_sha256"] = sha(G1)
            with open(os.path.join(pasta, "importar.txt"), "w", encoding="utf-8") as f:
                rc = subprocess.call([exe, "--data", data, "--saves-pasta", dst, "--saves-importar", G1, "--saves-nome", "BLUESTIN.000"],
                                     stdout=f, stderr=subprocess.STDOUT, cwd=os.path.dirname(exe))
            info["importar_rc"] = rc
            if rc != 0:
                raise SystemExit("importar o g1 falhou (%s)" % pasta)
    info["saves_inicio"] = cartao_hashes(dst)
    return dst, info


def executar(exe, data, nome, caso, pasta, extra_env=None, exe_sha=None):
    # caminhos absolutos: o jogo roda com a pasta do executavel como corrente (1a captura do N8.0 gravou os artefatos
    # e o cartao relativos a ela; as sessoes rodaram sem o cartao inicial e foram descartadas)
    pasta = os.path.abspath(pasta)
    os.makedirs(pasta)
    saves, cart = preparar_cartao(exe, data, caso, pasta)
    env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
    env.update({"BS_RELOGIO_FIXO": gv.RELOGIO, "BS_INPUT_SCRIPT": caso["roteiro"], "BS_STATE_PASSO_CADA": "1"})
    for art, var in LOGS.items():
        env[var] = os.path.join(pasta, art + ".tsv")
    # conferir --ambiente (negativos: BS_RAM_MUTACAO etc.); "{pasta}" vira a pasta do caso (N8.1: BS_INTC_TRACE={pasta}/intc.tsv)
    env.update({k: v.replace("{pasta}", pasta) for k, v in (extra_env or {}).items()})
    cmd = [exe, "--data", data, "--saves-pasta", saves] + caso["args"] + \
          ["--tempo", caso["tempo"], "--headless", str(caso["vblanks"]), "--dump", os.path.join(pasta, "quadro_final.bmp")]
    t0 = time.time()
    with open(os.path.join(pasta, "jogo.txt"), "w", encoding="utf-8") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(exe))
    info = {"caso": nome, "rc": rc, "segundos": round(time.time() - t0, 1), "cmd": cmd[1:],
            "exe_sha256": exe_sha or sha(exe), "disc_manifest_sha256": sha(os.path.join(data, "disc.manifest")),
            "ambiente": {k: v for k, v in env.items() if k.startswith("BS_")}, "saves": gv.hashes(saves),
            "saves_fim": cartao_hashes(saves), **cart}
    with open(os.path.join(pasta, "execucao.json"), "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    return info


# ---- leitura e normalizacao dos artefatos ----

def ler_json(p):
    if not os.path.exists(p):
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def linhas(p):
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8", errors="replace") as f:
        return [l.rstrip("\n") for l in f]


_CABECALHO = {"estado": "passo\t", "arquivos": "seq\t", "quadros": "vblank\t", "efeitos": "passo\t", "legendas": "vblank\t",
              "filme_legendas": "logico_ms\t", "cartao": "passo\t", "eventos": "seq\t", "relogios": "vblank\t"}


def dados(art, ls):
    """As linhas de dados de um artefato (sem comentario, cabecalho e, nas legendas, a linha do idioma)."""
    out = [l for l in ls if l and not l.startswith("#")]
    cab = _CABECALHO.get(art)
    if cab and out and out[0].startswith(cab):
        out = out[1:]
    if art == "legendas":
        out = [l for l in out if l.split("\t")[1:2] != ["idioma"]]
    return out


def norm(art, pasta, fim):
    p = os.path.join(pasta, art + ".tsv")
    ls = linhas(p)
    if ls is None:
        return None
    if art == "estado":  # passo, vblank, ...: compara o resto, ate o fim do roteiro
        return [l.split("\t")[0] + "\t" + "\t".join(l.split("\t")[2:]) for l in ls[1:] if l.split("\t")[0].isdigit() and
                int(l.split("\t")[0]) <= fim]
    if art == "arquivos":  # seq, vblank, origem, tipo, arquivo, deslocamento, bytes, sha
        return ["\t".join(l.split("\t")[3:]) for l in ls[1:]]
    if art == "quadros":  # vblank, quadro, entrada, passo: as trocas de quadro por passo
        out, ult = [], None
        for l in ls[1:]:
            c = l.split("\t")
            if len(c) >= 4 and c[3].isdigit() and int(c[3]) <= fim and (c[1], c[3]) != ult:
                out.append(c[3] + "\t" + c[1])
                ult = (c[1], c[3])
        return out
    if art in ("musica", "efeitos", "legendas", "filme_legendas", "cartao"):
        # tira as colunas de tempo (as duas primeiras numericas: tick/vblank e segundos ou ms)
        out = []
        for l in ls:
            c = l.split("\t")
            while c and re.fullmatch(r"-?\d+(\.\d+)?", c[0] or "x"):
                c = c[1:]
            out.append("\t".join(c))
        return out
    if art == "eventos":  # seq e ciclos fora (a coluna de ciclos mistura bases de tempo): tipo, a, b na ordem
        return ["\t".join(l.split("\t")[2:5]) for l in dados(art, ls)]
    return ls


# ---- validacao de um caso (funcao pura sobre a pasta; sem rodar o jogo) ----

def _passos_estado(pasta):
    ls = linhas(os.path.join(pasta, "estado.tsv")) or []
    out = []
    for l in dados("estado", ls):
        c = l.split("\t")
        if len(c) >= 2 and c[0].isdigit() and c[1].isdigit():
            out.append((int(c[0]), int(c[1])))
    return out


def validar_caso(pasta, caso, esperado, oraculo=None):
    """Problemas do caso (lista vazia = valido). `caso`: a entrada do manifesto; `esperado`: a linha da ESPERADO;
    `oraculo`: para as sessoes, {"saves_fim": {rel: hash}, "cartao": {rel: hash}, "bruto_norm": {bruto: normalizado}}."""
    p = []
    fim = caso["fim"]
    info = None
    ij = os.path.join(pasta, "execucao.json")
    if os.path.exists(ij):
        with open(ij, encoding="utf-8") as f:
            info = json.load(f)
    else:
        p.append("execucao.json ausente")
    if info is not None and info.get("rc") != 0:
        p.append("rc=%s" % info.get("rc"))
    saida = "\n".join(linhas(os.path.join(pasta, "jogo.txt")) or [])
    # vivacidade: uma linha "erro:" (sh4_fatal; no experimental a guarda de falta de progresso e fatal e sai assim), um
    # deadlock contado ou a interrupcao nunca reconhecida. Os contadores normais ("deadlock 0", "faltas de progresso N"
    # do modo sombra no --tempo nativo, so diagnostico) nao reprovam
    erro = re.search(r"(?m)^erro:.*$", saida)
    if erro:
        p.append("saida do jogo: %s" % erro.group(0).strip())
    for padrao in (r"deadlock [1-9]", r"interrupcao nunca reconhecida"):
        if re.search(padrao, saida):
            p.append("saida do jogo: %s" % padrao)
    # artefatos obrigatorios: existem e tem dados
    for art in LOGS:
        req = esperado.get(art, _V)
        ls = linhas(os.path.join(pasta, art + ".tsv"))
        if req == _O:
            if ls is None:
                p.append("%s ausente" % art)
            elif not dados(art, ls):
                p.append("%s vazio" % art)
    # estado: exatamente 1, 2, ..., N na ordem, com N >= fim (depois do fim do roteiro o jogo segue ate o orcamento de
    # VBlanks, e os passos continuam consecutivos); passo 0, reordenado, repetido ou buraco reprova
    st = _passos_estado(pasta)
    vistos = {}
    for passo, _ in st:
        vistos[passo] = vistos.get(passo, 0) + 1
    falt = [n for n in range(1, fim + 1) if n not in vistos]
    if falt:
        p.append("estado: passo %d ausente%s" % (falt[0], " (e mais %d)" % (len(falt) - 1) if len(falt) > 1 else ""))
    rep = sorted(n for n, k in vistos.items() if k > 1)
    if rep:
        p.append("estado: passo %d repetido" % rep[0])
    fora = next((k for k, (passo, _) in enumerate(st) if passo != k + 1), None)
    if fora is not None and not falt and not rep:
        p.append("estado: passo %d na linha %d (fora de ordem ou fora da rota)" % (st[fora][0], fora + 1))
    # logs que tem de cobrir a rota inteira: quadros ate o passo fim; eventos e relogios ate o VBlank do passo fim
    vb_fim = next((vb for passo, vb in st if passo == fim), None)
    q = linhas(os.path.join(pasta, "quadros.tsv"))
    if q is not None and dados("quadros", q):
        ult = max((int(l.split("\t")[3]) for l in dados("quadros", q) if len(l.split("\t")) >= 4 and l.split("\t")[3].isdigit()), default=-1)
        if ult < fim:
            p.append("quadros: log incompleto (ultimo passo %d de %d)" % (ult, fim))
    if vb_fim is not None:
        ev = linhas(os.path.join(pasta, "eventos.tsv"))
        if ev is not None and dados("eventos", ev):
            vs = [int(l.split("\t")[3], 16) for l in dados("eventos", ev) if l.split("\t")[2:3] == ["V"]]
            if not vs or max(vs) < vb_fim:
                p.append("eventos: log incompleto (ultimo VBlank-In %s, passo %d no VBlank %d)" % (max(vs) if vs else "nenhum", fim, vb_fim))
        rl = linhas(os.path.join(pasta, "relogios.tsv"))
        if rl is not None and dados("relogios", rl):
            ult = max((int(l.split("\t")[0]) for l in dados("relogios", rl) if l.split("\t")[0].isdigit()), default=-1)
            if ult < vb_fim:
                p.append("relogios: log incompleto (ultimo VBlank %d, passo %d no VBlank %d)" % (ult, fim, vb_fim))
    # cartao: operacoes exigidas, nenhuma falha
    ops = {}
    for l in dados("cartao", linhas(os.path.join(pasta, "cartao.tsv")) or []):
        c = l.split("\t")
        if len(c) >= 3:
            ops[(c[1], c[2])] = ops.get((c[1], c[2]), 0) + 1
    for (op, res), k in sorted(ops.items()):
        if res != "ok":
            p.append("cartao: %d %s com %s" % (k, op, res))
    for op, minimo in (esperado.get("cartao_ops") or {}).items():
        if ops.get((op, "ok"), 0) < minimo:
            p.append("cartao: %s esperado (>= %d ok, houve %d)" % (op.upper(), minimo, ops.get((op, "ok"), 0)))
    if info is None:
        return p
    # cartao inicial e saves do fim
    ini = info.get("saves_inicio")
    fimc = info.get("saves_fim")
    if ini is None or fimc is None:
        p.append("execucao.json sem o cartao inicial ou final")
        return p
    if caso["tipo"] == "roteiro" and caso.get("cartao") == "g1":
        if info.get("g1_sha256") != G1_SHA256:
            p.append("cartao inicial: g1 de origem diferente (%s)" % info.get("g1_sha256"))
        if info.get("importar_rc") != 0 or list(ini) != ["card0/BLUESTIN.000.bsv"]:
            p.append("cartao inicial: importacao do g1 (rc %s, arquivos %s)" % (info.get("importar_rc"), sorted(ini)))
    elif caso["tipo"] == "roteiro":
        if ini:
            p.append("cartao inicial: devia estar vazio (%s)" % sorted(ini))
    else:
        esp = (oraculo or {}).get("cartao")
        if esp is None:
            p.append("cartao inicial: sem o oraculo da sessao")
        else:
            p += _confere_cartao("cartao inicial", ini, esp, (oraculo or {}).get("bruto_norm", {}))
    modo = esperado.get("saves_fim")
    if modo == "vazio" and fimc:
        p.append("saves do fim: a rota nao grava, mas ha %s" % sorted(fimc))
    elif modo == "inicio" and {k: v["normalizado"] for k, v in fimc.items()} != {k: v["normalizado"] for k, v in ini.items()}:
        p.append("saves do fim: diferentes do cartao inicial")
    elif modo == "sessao":
        esp = (oraculo or {}).get("saves_fim")
        if esp is None:
            p.append("saves do fim: sem o oraculo da sessao")
        else:
            p += _confere_cartao("saves do fim", fimc, esp, (oraculo or {}).get("bruto_norm", {}))
    return p


def _confere_cartao(rotulo, real, esperado, bruto_norm):
    """real: {rel: {bruto, normalizado}}; esperado: {rel: hash} gravado no sessao.json no hash bruto OU no normalizado
    (as duas sessoes usaram formatos diferentes). Um hash bruto que nao bate (a data do envelope e a do relogio do host)
    vale pelo normalizado conhecido do mesmo arquivo (bruto_norm, dos cartoes reais das sessoes)."""
    p = []
    if sorted(real) != sorted(esperado):
        return ["%s: arquivos %s, esperado %s" % (rotulo, sorted(real), sorted(esperado))]
    for rel, h in sorted(esperado.items()):
        r = real[rel]
        if h not in (r["bruto"], r["normalizado"]) and bruto_norm.get(h) != r["normalizado"]:
            p.append("%s: %s diferente" % (rotulo, rel))
    return p


def oraculo_sessao(sessao):
    s = gv.ler_sessao(sessao)
    return {"saves_fim": s.get("saves_fim"), "cartao": (s.get("origem") or {}).get("cartao"), "bruto_norm": bruto_para_normalizado()}


def validar_configuracao(manifesto, infos):
    """Configuracao unica: o mesmo executavel, dados e ambiente (fora os caminhos) em todos os casos."""
    p = []
    amb0 = None
    for n, i in sorted(infos.items()):
        if i.get("exe_sha256") != manifesto.get("exe_sha256"):
            p.append("%s: configuracao diferente (exe_sha256)" % n)
        if i.get("disc_manifest_sha256") != manifesto.get("disc_manifest_sha256"):
            p.append("%s: configuracao diferente (dados)" % n)
        amb = {k: v for k, v in (i.get("ambiente") or {}).items() if k not in _CAMINHOS}
        if amb.get("BS_RELOGIO_FIXO") != manifesto.get("relogio"):
            p.append("%s: configuracao diferente (BS_RELOGIO_FIXO %s)" % (n, amb.get("BS_RELOGIO_FIXO")))
        if amb0 is None:
            amb0 = (n, amb)
        elif amb != amb0[1]:
            p.append("%s: ambiente diferente de %s (%s)" % (n, amb0[0], sorted(set(amb.items()) ^ set(amb0[1].items()))))
    return p


# ---- comparacao de um caso (funcao pura sobre as duas pastas) ----

def intervalos(evs):
    """eventos normalizados (tipo, a, b) -> [(VBlank que abre o intervalo ou None no inicio, [eventos ate o proximo V])]"""
    out = [(None, [])]
    for e in evs:
        t, a, b = (e.split("\t") + ["", "", ""])[:3]
        if t == "V":
            out.append((int(a, 16), []))
        else:
            out[-1][1].append((t, a, b))
    return out


def carregar_excecoes(path):
    """Excecoes de SPC, uma por diferenca: [[spc]] caso, vblank, intevt, spc_ref, spc_novo, justificativa, evidencia."""
    if not path:
        return []
    with open(path, "rb") as f:
        d = tomllib.load(f)
    out = []
    for e in d.get("spc", []):
        falta = [k for k in ("caso", "vblank", "intevt", "spc_ref", "spc_novo", "justificativa", "evidencia") if not e.get(k)]
        if falta:
            raise SystemExit("excecao de SPC incompleta (%s): falta %s" % (e, falta))
        out.append({"caso": e["caso"], "vblank": int(e["vblank"]), "intevt": "%08X" % int(str(e["intevt"]), 16),
                    "spc_ref": "%08X" % int(str(e["spc_ref"]), 16), "spc_novo": "%08X" % int(str(e["spc_novo"]), 16),
                    "justificativa": e["justificativa"], "evidencia": e["evidencia"]})
    return out


def comparar_eventos(nome, r, x, excecoes):
    """(diferencas, excecoes usadas). Por intervalo entre dois VBlank-In, a sequencia ordenada estrita."""
    ir, ix = intervalos(r), intervalos(x)
    usadas = []  # cada entrada vale para uma unica diferenca: consumida ao casar
    if [v for v, _ in ir] != [v for v, _ in ix]:
        k = next((j for j in range(min(len(ir), len(ix))) if ir[j][0] != ix[j][0]), min(len(ir), len(ix)))
        return ["eventos: VBlank-In diferentes (%d x %d intervalos; primeiro diferente no %d)" % (len(ir), len(ix), k)], usadas
    for j, ((vb, a), (_, b)) in enumerate(zip(ir, ix)):
        if a == b:
            continue
        rot = "inicio" if vb is None else "VBlank %d" % vb
        if len(a) == len(b):
            ok, uso = True, []
            for ea, eb in zip(a, b):
                if ea == eb:
                    continue
                exc = next((e for e in excecoes if ea[0] == eb[0] == "I" and ea[1] == eb[1] and e["caso"] == nome and
                            e["vblank"] == vb and e["intevt"] == ea[1] and e["spc_ref"] == ea[2] and e["spc_novo"] == eb[2]
                            and not any(e is u for u in usadas + uso)), None)
                if exc is None:
                    ok = False
                    break
                uso.append(exc)
            if ok:
                usadas += uso
                continue
        pos = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        ea = a[pos] if pos < len(a) else None
        eb = b[pos] if pos < len(b) else None
        tipo = "ordem"
        if ea and eb and ea[0] == eb[0] == "I" and ea[1] == eb[1]:
            tipo = "SPC %s x %s" % (ea[2], eb[2])
        elif ea and ea not in b:
            seg = ix[j + 1][1] if j + 1 < len(ix) else []
            ant = ix[j - 1][1] if j > 0 else []
            tipo = "entrega atrasada" if ea in seg else "entrega antecipada" if ea in ant else "evento ausente"
        elif eb and eb not in a:
            tipo = "evento a mais"
        return ["eventos: %s no intervalo %d (%s), posicao %d: %s x %s" % (tipo, j, rot, pos, ea, eb)], usadas
    return [], usadas


def comparar(ref, nova, nome, caso, esperado, excecoes=()):
    """(diferencas, excecoes usadas) entre duas pastas do mesmo caso. Artefato obrigatorio ausente ou vazio nos dois
    lados e diferenca (nao passa por [] == [])."""
    dif = []
    ri, xi = ler_json(os.path.join(ref, "execucao.json")), ler_json(os.path.join(nova, "execucao.json"))
    if xi.get("rc") != 0:
        dif.append("rc=%s" % xi.get("rc"))
    if ri.get("saves") != xi.get("saves"):
        dif.append("saves")
    if {k: v.get("normalizado") for k, v in (ri.get("saves_inicio") or {}).items()} != \
       {k: v.get("normalizado") for k, v in (xi.get("saves_inicio") or {}).items()}:
        dif.append("cartao inicial")
    fim = caso["fim"]
    usadas = []
    for art in LOGS:
        r, x = norm(art, ref, fim), norm(art, nova, fim)
        if esperado.get(art, _V) == _O:
            for lado, v in (("referencia", r), ("nova", x)):
                if v is None:
                    dif.append("%s ausente na %s" % (art, lado))
                elif not dados(art, linhas(os.path.join(ref if lado == "referencia" else nova, art + ".tsv"))):
                    dif.append("%s vazio na %s" % (art, lado))
        if art == "eventos" and r is not None and x is not None:
            d, u = comparar_eventos(nome, r, x, excecoes)
            dif += d
            usadas += u
            continue
        if r != x:
            k = next((j for j in range(min(len(r or []), len(x or []))) if r[j] != x[j]), min(len(r or []), len(x or [])))
            det = ""
            if art == "estado" and r and x and k < min(len(r), len(x)):
                det = ", passo %s" % r[k].split("\t")[0]
            dif.append("%s (%d x %d linhas; primeira diferenca na %d%s)" % (art, len(r or []), len(x or []), k, det))
    return dif, usadas


def comparar_conjunto(ref_casos, novos):
    """Casos do manifesto sem execucao nova, e execucoes de casos fora do manifesto."""
    p = ["caso %s sem execucao nova" % n for n in sorted(set(ref_casos) - set(novos))]
    p += ["caso %s fora do manifesto" % n for n in sorted(set(novos) - set(ref_casos))]
    return p


def _oraculo(caso):
    return oraculo_sessao(caso["sessao"]) if caso["tipo"] == "sessao" else None


# ---- comandos ----

def capturar(a):
    todos = casos()
    falta = sorted(CASOS_N8 - set(todos))
    if not a.casos and falta:
        raise SystemExit("captura completa sem %s (sessao ou roteiro ausente): nada capturado" % ", ".join(falta))
    escolhidos = a.casos.split(",") if a.casos else sorted(CASOS_N8)
    desconhecidos = [n for n in escolhidos if n not in todos]
    if desconhecidos:
        raise SystemExit("casos desconhecidos ou sem gravacao: %s" % ", ".join(desconhecidos))
    if os.path.exists(a.saida) and os.listdir(a.saida):
        raise SystemExit("%s ja existe e nao esta vazia (referencias nunca sao sobrescritas)" % a.saida)
    a.saida = os.path.abspath(a.saida)
    os.makedirs(a.saida, exist_ok=True)
    exe, data = os.path.abspath(a.exe), os.path.abspath(a.data)
    exe_sha = sha(exe)
    manifesto = {"criado": time.strftime("%Y-%m-%d %H:%M:%S"), "commit": git_commit(), "exe": exe, "exe_sha256": exe_sha,
                 "data": data, "disc_manifest_sha256": sha(os.path.join(data, "disc.manifest")), "relogio": gv.RELOGIO,
                 "parcial": set(escolhidos) != CASOS_N8, "casos": {n: todos[n] for n in escolhidos}}
    with open(os.path.join(a.saida, "manifesto.json"), "w", encoding="utf-8") as f:
        json.dump(manifesto, f, indent=2)
    infos = {}
    with cf.ThreadPoolExecutor(a.paralelo) as ex:
        fut = {ex.submit(executar, exe, data, n, todos[n], os.path.join(a.saida, n), None, exe_sha): n for n in escolhidos}
        for f in cf.as_completed(fut):
            n = fut[f]
            infos[n] = f.result()
            print("[rodou] %s: rc=%d, %.0f s" % (n, infos[n]["rc"], infos[n]["segundos"]), flush=True)
    return validar(argparse.Namespace(ref=a.saida))


def validar(a):
    ref = os.path.abspath(a.ref)
    with open(os.path.join(ref, "manifesto.json"), encoding="utf-8") as f:
        man = json.load(f)
    falhas, infos = 0, {}
    for n, caso in sorted(man["casos"].items()):
        pasta = os.path.join(ref, n)
        if os.path.exists(os.path.join(pasta, "execucao.json")):
            infos[n] = ler_json(os.path.join(pasta, "execucao.json"))
        p = validar_caso(pasta, caso, ESPERADO[base_do_caso(n)], _oraculo(caso))
        falhas += bool(p)
        st = _passos_estado(pasta)
        print("[%s] %s: %d passos de %d%s" % ("ok" if not p else "FALHA", n, len({s for s, _ in st if s <= caso["fim"]}), caso["fim"],
                                              (": " + "; ".join(p)) if p else ""))
    cfg = validar_configuracao(man, infos)
    if not man.get("parcial") and set(man["casos"]) != CASOS_N8:
        # manifesto sem a marca de parcial (inclusive os anteriores a ela) vale como captura completa: os 16 exatos
        cfg.append("conjunto de casos: faltam %s; a mais %s" % (sorted(CASOS_N8 - set(man["casos"])) or "nenhum",
                                                                   sorted(set(man["casos"]) - CASOS_N8) or "nenhum"))
    for c in cfg:
        print("[FALHA] %s" % c)
    print("%d casos, %d com falha%s; referencias em %s" % (len(man["casos"]), falhas, ", configuracao diferente" if cfg else "", ref))
    return 1 if falhas or cfg else 0


def conferir(a):
    a.ref = os.path.abspath(a.ref)
    with open(os.path.join(a.ref, "manifesto.json"), encoding="utf-8") as f:
        man = json.load(f)
    todos = man["casos"]
    escolhidos = a.casos.split(",") if a.casos else list(todos)
    exe = os.path.abspath(a.exe)
    exe_sha = sha(exe)
    excecoes = carregar_excecoes(a.excecoes)
    if excecoes and exe_sha == man["exe_sha256"]:
        raise SystemExit("o mesmo executavel da referencia: excecoes nao valem (a reprodutibilidade exige igualdade)")
    data = os.path.abspath(a.data or man["data"])
    base = tempfile.mkdtemp(prefix="n8_conferir_")
    falhas, infos = 0, {}
    with cf.ThreadPoolExecutor(a.paralelo) as ex:
        extra = dict(x.split("=", 1) for x in a.ambiente)
        fut = {ex.submit(executar, exe, data, n, todos[n], os.path.join(base, n), extra, exe_sha): n for n in escolhidos}
        for f in cf.as_completed(fut):
            n = fut[f]
            infos[n] = f.result()
            esp = ESPERADO[base_do_caso(n)]
            dif = ["validacao: " + x for x in validar_caso(os.path.join(base, n), todos[n], esp, _oraculo(todos[n]))]
            d, usadas = comparar(os.path.join(a.ref, n), os.path.join(base, n), n, todos[n], esp, excecoes)
            dif += d
            falhas += bool(dif)
            print("[%s] %s%s" % ("igual" if not dif else "DIFERENTE", n, (": " + "; ".join(dif)) if dif else ""), flush=True)
            for e in usadas:
                print("    excecao usada: VBlank %d, %s, SPC %s -> %s (%s)" % (e["vblank"], e["intevt"], e["spc_ref"], e["spc_novo"], e["justificativa"]))
    conj = comparar_conjunto(escolhidos, infos)
    for c in conj:
        print("[DIFERENTE] %s" % c)
    print("%d casos, %d diferentes%s (execucoes em %s)" % (len(escolhidos), falhas, " (subconjunto do manifesto)" if a.casos else "", base))
    return 1 if falhas or conj else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capturar")
    c.add_argument("--exe", required=True)
    c.add_argument("--data", required=True)
    c.add_argument("--saida", required=True)
    c.add_argument("--casos", default="")
    c.add_argument("--paralelo", type=int, default=4)
    k = sub.add_parser("conferir")
    k.add_argument("--exe", required=True)
    k.add_argument("--ref", required=True)
    k.add_argument("--data", default="")
    k.add_argument("--casos", default="")
    k.add_argument("--paralelo", type=int, default=4)
    k.add_argument("--ambiente", action="append", default=[], help="K=V a mais no ambiente do jogo (negativo: BS_RAM_MUTACAO=...)")
    k.add_argument("--excecoes", default="", help="arquivo .toml com as excecoes de SPC (uma por diferenca, com evidencia)")
    v = sub.add_parser("validar")
    v.add_argument("--ref", required=True)
    a = ap.parse_args()
    return capturar(a) if a.cmd == "capturar" else conferir(a) if a.cmd == "conferir" else validar(a)


if __name__ == "__main__":
    sys.exit(main())
