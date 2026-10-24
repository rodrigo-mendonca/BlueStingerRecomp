"""N2: as sessoes gravadas (tools/dev/gravacoes.py, gravadas no tempo estavel) reproduzidas num modo de tempo, com o
estado por passo de jogo e os saves conferidos contra a gravacao. No tempo estavel tudo tem de ser igual; no
experimental valem so as regras da lista fechada config/n2_excecoes.toml (hoje "passos-isolados-por-load": cada passo
diferente vem logo depois do passo em que um LOAD do cartao foi concluido, registrado pelo jogo em BS_SAVES_LOG (o
estado e o do inicio de cada passo), um passo por LOAD, e o passo seguinte e igual). Saves comparados sem a data do envelope.

    python tools/dev/n2_sessoes.py --exe <bluestinger.exe> [--tempo nativo|experimental] [--data <data>]
                                   [--mutacao <vblank>:<endereco>] [sessao ...]

Sai com 0 se todas as sessoes passam (pelo menos uma). As reproducoes ficam em %TEMP%\\bs-n2-sessoes (apagadas a cada
execucao).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import tomllib

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import gravacoes as gv  # noqa: E402

REPO = os.path.dirname(os.path.dirname(AQUI))
EXCECOES = os.path.join(REPO, "config", "n2_excecoes.toml")
REGRAS_CONHECIDAS = {"passos-isolados-por-load"}


def regras_ativas():
    """As regras aplicaveis da lista fechada; uma regra desconhecida e erro (a lista e o verificador andam juntos)."""
    with open(EXCECOES, "rb") as f:
        itens = tomllib.load(f).get("excecao", [])
    regras, ids = {}, set()
    for e in itens:
        if e.get("id") in ids:
            raise SystemExit("%s: id repetido %r" % (EXCECOES, e.get("id")))
        ids.add(e.get("id"))
        if e.get("regra") in regras:
            raise SystemExit("%s: regra %r em mais de uma excecao (%r, %r)" % (EXCECOES, e["regra"], regras[e["regra"]], e["id"]))
        for campo in ("id", "classe", "onde", "efeito", "causa", "evidencia"):
            if not e.get(campo):
                raise SystemExit("%s: excecao %r sem o campo %r" % (EXCECOES, e.get("id"), campo))
        if "regra" in e:
            if e["regra"] not in REGRAS_CONHECIDAS:
                raise SystemExit("%s: regra desconhecida %r em %r" % (EXCECOES, e["regra"], e["id"]))
            regras[e["regra"]] = e["id"]
    return regras


def ler_ops(caminho):
    """Conclusoes de LOAD/SAVE registradas por BS_SAVES_LOG: lista de (passo, op, resultado)."""
    ops = []
    if os.path.exists(caminho):
        with open(caminho, encoding="utf-8") as f:
            next(f, None)
            for l in f:
                p = l.rstrip("\n").split("\t")
                if len(p) == 3:
                    ops.append((int(p[0]), p[1], p[2]))
    return ops


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe")
    ap.add_argument("--reavaliar", action="store_true",
                    help="confere de novo as reproducoes da ultima execucao (mesmo --tempo), sem rodar o jogo")
    ap.add_argument("--negativo-sem-loads", action="store_true",
                    help="negativo do verificador (experimental): ignora o registro de LOADs (BS_SAVES_LOG); cada sessao tem de "
                         "ter diferencas e ser rejeitada so pela falta dos LOADs, e entao o negativo sai com 0")
    ap.add_argument("--tempo", default="experimental", choices=["nativo", "experimental"])
    ap.add_argument("--data", default=os.path.join(REPO, "dist", "BlueStinger-Preview", "data"))
    ap.add_argument("--mutacao", default="", help="negativo: BS_RAM_MUTACAO=<vblank>:<endereco> (um byte invertido); tem de falhar")
    ap.add_argument("--arg", action="append", default=[], dest="args_extras",
                    help="um argumento a mais do jogo por ocorrencia (ex.: --arg=--arquivos --arg=legado; D9(a)/U6)")
    ap.add_argument("sessoes", nargs="*")
    a = ap.parse_args()
    if not a.exe and not a.reavaliar:
        ap.error("--exe e obrigatorio (salvo com --reavaliar)")
    if a.exe:
        a.exe = os.path.abspath(a.exe)  # o jogo roda com a pasta do executavel como corrente
    if a.reavaliar and (a.mutacao or a.args_extras):
        ap.error("--mutacao e --arg exigem rodar o jogo (nao combinam com --reavaliar)")
    if a.negativo_sem_loads and a.tempo != "experimental":
        ap.error("--negativo-sem-loads so vale no --tempo experimental")
    regras = regras_ativas() if a.tempo == "experimental" else {}
    sessoes = a.sessoes or [s for s in sorted(os.listdir(gv.RAIZ)) if os.path.exists(os.path.join(gv.RAIZ, s, "sessao.json"))]
    if not sessoes:
        print("nenhuma sessao gravada")
        return 1
    base = os.path.join(os.environ.get("TEMP", "."), "bs-n2-sessoes")
    if not a.reavaliar:
        shutil.rmtree(base, ignore_errors=True)
    falhas = 0
    for nome in sessoes:
        d, s = gv.pasta(nome), gv.ler_sessao(nome)
        rod = gv.rodape(os.path.join(d, "entrada.txt"))
        if not rod:
            print("[FALHA] %s: sem rodape (gravacao incompleta)" % nome)
            falhas += 1
            continue
        fim, vb = rod
        gravado = gv.ler_estado(os.path.join(d, "estado.tsv"))
        passos = [k for k in sorted(gravado) if k <= fim]
        # a referencia tem de cobrir todos os passos de 1 ate o fim, sem buracos (nada passa por vacuidade)
        if fim < 1 or passos != list(range(1, fim + 1)):
            print("[FALHA] %s: referencia vazia ou com buracos (%d passos ate o %d)" % (nome, len(passos), fim))
            falhas += 1
            continue
        r = os.path.join(base, nome)
        t0 = time.time()
        if a.reavaliar:
            try:
                with open(os.path.join(r, "rc.txt"), encoding="utf-8") as f:
                    tempo, rc = f.read().split()
                rc = int(rc)
            except (OSError, ValueError):
                print("[FALHA] %s: sem reproducao anterior para reavaliar" % nome)
                falhas += 1
                continue
            if tempo != a.tempo:
                print("[FALHA] %s: a reproducao anterior e do tempo %s, nao %s" % (nome, tempo, a.tempo))
                falhas += 1
                continue
        else:
            os.makedirs(r)
            shutil.copytree(os.path.join(d, "saves_inicio"), os.path.join(r, "saves"))
            env = {k: v for k, v in os.environ.items() if not k.startswith("BS_")}
            env.update({"BS_RELOGIO_FIXO": gv.RELOGIO, "BS_INPUT_SCRIPT": os.path.join(d, "entrada.txt"),
                        "BS_STATE_PASSO": os.path.join(r, "estado.tsv"), "BS_STATE_PASSO_CADA": "1",
                        "BS_SAVES_LOG": os.path.join(r, "ops.tsv")})
            if a.mutacao:
                env["BS_RAM_MUTACAO"] = a.mutacao
            # o experimental termina antes (cargas sem VBlanks); a folga cobre o estavel com a espera do download da musica
            # o modo pedido vale sobre o da sessao (sessoes novas gravam --tempo; o ultimo argumento vence)
            cmd = [a.exe, "--data", a.data, "--saves-pasta", os.path.join(r, "saves")] + s["args"].split() + \
                a.args_extras + ["--tempo", a.tempo, "--headless", str(vb + 600)]
            with open(os.path.join(r, "jogo.txt"), "w", encoding="utf-8") as f:
                rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(a.exe))
            with open(os.path.join(r, "rc.txt"), "w", encoding="utf-8") as f:
                f.write("%s %d\n" % (a.tempo, rc))
        rep = gv.ler_estado(os.path.join(r, "estado.tsv"))
        faltam = [k for k in passos if k not in rep]
        dif = [k for k in passos if k in rep and rep[k][2:] != gravado[k][2:]]
        # so as conclusoes de LOAD bem-sucedidas ate o ultimo passo comparado
        loads = [] if a.negativo_sem_loads else \
            sorted({p for p, op, res in ler_ops(os.path.join(r, "ops.tsv")) if op == "load" and res == "ok" and p <= fim})
        saves = gv.hashes(os.path.join(r, "saves")) == gv.hashes(os.path.join(d, "saves"))
        base_ok = rc == 0 and not faltam and saves
        if a.tempo == "nativo" or not dif:
            ok = base_ok and not dif
            regra = "igualdade estrita"
        elif "passos-isolados-por-load" in regras:
            # cada linha diferente vem logo depois do passo de uma conclusao de LOAD (um para um: o estado e gravado no
            # inicio do passo, entao um LOAD concluido no passo k aparece na linha k + 1) e a linha seguinte volta a ser igual
            sem_load = [k for k in dif if k - 1 not in loads]
            nao_isolados = [k for k in dif if not (k + 1 <= fim and rep.get(k + 1, [None])[2:] == gravado[k + 1][2:])]
            ok = base_ok and not sem_load and not nao_isolados
            regra = "%s: %d passo(s) diferente(s), LOADs nos passos %s%s%s" % (
                regras["passos-isolados-por-load"], len(dif), loads[:8],
                ", sem LOAD %s" % sem_load[:5] if sem_load else "", ", nao isolados %s" % nao_isolados[:5] if nao_isolados else "")
        else:
            ok = False
            regra = "nenhuma regra da lista cobre as diferencas"
        if a.negativo_sem_loads:
            # o negativo so vale exercitado: a sessao passaria com os LOADs e falha so pela falta deles
            rejeitada = bool(dif) and base_ok and "passos-isolados-por-load" in regras and sem_load and not nao_isolados
            regra = "negativo %s: %s" % ("rejeitado pela falta dos LOADs" if rejeitada else
                                         "NAO exercitado (sem diferencas) ou rejeitado por outro motivo", regra)
            ok = rejeitada
        falhas += not ok
        print("[%s] %s (%s): rc=%d, %d passos, %d diferentes %s, %d faltando, saves %s; %s; %.0f s" % (
            "ok" if ok else "FALHA", nome, a.tempo, rc, len(passos), len(dif), dif[:5], len(faltam), "iguais" if saves else "DIFERENTES",
            regra, time.time() - t0))
    print("%d sessoes, %d falhas" % (len(sessoes), falhas))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
