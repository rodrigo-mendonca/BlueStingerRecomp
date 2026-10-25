"""N8.1 (docs/N8-1-PREPARACAO.md): verificacoes S2n e S4n com o audio nativo, sobre os artefatos de uma execucao.
Nada aqui roda o jogo; a suite tools/dist/test_n8_nativo.ps1 roda e chama estas conferencias.

S2n (espera do VBlank, 8C064A2C): o trace de progresso (BS_PROGRESS_TRACE) registra cada episodio de espera: `inicio`,
`avanco`, `fim` (com a mascara dos eventos entregues no episodio e o ultimo antes da saida). Exige, diretamente nos
episodios:
  - a execucao completa (rc 0 pela suite, "VBlanks: N;", nenhuma linha "erro:") e o trace inteiro: cabecalho, toda
    linha com as 9 colunas, os registros `quadro` ate o VBlank N;
  - cada episodio fechado pelo evento esperado (VBlank-In): saidas sem evento = 0 e saidas liberadas por outro
    evento = 0 (as duas sao saidas indevidas), com o evento esperado na mascara;
  - inicio/fim alternados por thread; no fim, no maximo um episodio aberto, e so se a execucao terminou dentro dele:
    comecou no ultimo campo (VBlank >= N - 1), depois do ultimo `fim`, sem outra espera da thread depois;
  - o resumo do monitor ([progresso] espera ...) igual a contagem do trace.
S4n (sequencia do servidor do CRI): o trace de chamadas (BS_CALL_TRACE de 8C1586B8 e 8C174888) do tempo legado e do
modo sombra, com o audio nativo, iguais linha a linha (funcao, chamador, r4-r7, r0, pr, VBlank); as duas execucoes
completas, o trace com seq contiguo e ate o ultimo VBlank (N - 1). Nao usa contagem calibrada no audio legado.

A assinatura (SHA-256 dos episodios ou das chamadas) vira a referencia pre-migracao: `--json` grava o resumo, e
`--ref <json>` exige a mesma assinatura (estrito).

    python tools/dev/n8_nativo.py s2n --trace <pg.tsv> --saida <saida.txt> --vblanks N [--json <f>] [--ref <f>]
    python tools/dev/n8_nativo.py s4n --legado <calls> --sombra <calls> --saida-legado <txt> --saida-sombra <txt>
                                      --vblanks N [--neg <calls> --saida-neg <txt>] [--json <f>] [--ref <f>]
    python tools/dev/n8_nativo.py s4seq --calls <calls> --saida <txt> --vblanks N [--json <f>] [--ref <f>]
"""
import argparse
import hashlib
import json
import re
import sys

ESPERA_VBLANK = 0x8C064A2C
VBLANK_IN = 0  # EventKind::VBlankIn (native/platform/time.h)
CAB_PG = "reg\tvblank\tthread\tespera\tinstante\talvo\tvalor\teventos\tliberado"
CAB_CALLS = "seq\tfuncao\tchamador\tr4\tr5\tr6\tr7\tr0\tpr\tvblank\tstrings"


class Falha(Exception):
    pass


def execucao_completa(saida, vblanks):
    """Lista de problemas da saida do jogo (vazia = completa)."""
    p = []
    if not re.search(r"(?m)^VBlanks: %d;" % vblanks, saida):
        p.append("execucao incompleta: sem 'VBlanks: %d;'" % vblanks)
    m = re.search(r"(?m)^erro:.*$", saida)
    if m:
        p.append("erro na saida: " + m.group(0)[:160])
    return p


def resumo_espera(saida, espera):
    m = re.search(r"\[progresso\] espera %08X: episodios (\d+), voltas \d+, avancos \d+, saidas com evento (\d+), "
                  r"sem evento (\d+), deadlock (\d+);.*?(?:evento esperado \w+: com (\d+), sem (\d+))?; maior" % espera, saida)
    if not m:
        return None
    g = [int(x) if x is not None else None for x in m.groups()]
    return {"episodios": g[0], "com_evento": g[1], "sem_evento": g[2], "deadlock": g[3], "com_esperado": g[4],
            "sem_esperado": g[5]}


def s2n(trace_linhas, saida, vblanks, espera=ESPERA_VBLANK, evento=VBLANK_IN):
    """Confere a espera `espera`. Devolve (problemas, resumo). trace_linhas: as linhas do pg.tsv (sem o \\n), e a
    ultima tem de ter terminado com \\n (quem le marca a linha truncada com None)."""
    prob = execucao_completa(saida, vblanks)
    if len(trace_linhas) < 2 or not trace_linhas[0].startswith("# ") or trace_linhas[1] != CAB_PG:
        return prob + ["trace de progresso sem cabecalho"], {}
    aberto = {}          # thread -> (vblank do inicio, indice)
    ultimo_fim = -1      # VBlank do ultimo fim da espera
    ultimo_quadro = -1
    episodios = fins = sem_evento = outro_evento = sem_mascara = 0
    depois = {}          # thread -> registros de outra espera depois do inicio aberto
    h = hashlib.sha256()
    for i, l in enumerate(trace_linhas[2:], 3):
        if l is None:
            prob.append("trace truncado na linha %d (sem fim de linha)" % i)
            break
        f = l.split("\t")
        if len(f) != 9:
            prob.append("linha %d com %d colunas" % (i, len(f)))
            break
        try:
            vb, th, inst = int(f[1]), int(f[2]), int(f[4])
            mask, lib = int(f[7], 16), int(f[8])
        except ValueError:
            prob.append("linha %d ilegivel" % i)
            break
        if f[0] == "quadro":
            ultimo_quadro = max(ultimo_quadro, vb)
            continue
        if f[3] != "%08X" % espera:
            if th in aberto and f[0] in ("inicio", "fim"):
                depois[th] = depois.get(th, 0) + 1
            continue
        if f[0] == "inicio":
            if th in aberto:
                prob.append("linha %d: inicio com episodio aberto na thread %d" % (i, th))
            aberto[th] = (vb, i)
            episodios += 1
            depois.pop(th, None)
        elif f[0] == "fim":
            if th not in aberto:
                prob.append("linha %d: fim sem inicio na thread %d" % (i, th))
            aberto.pop(th, None)
            fins += 1
            ultimo_fim = max(ultimo_fim, vb)
            if lib == -1:
                sem_evento += 1
            elif lib != evento:
                outro_evento += 1
            if not mask & (1 << evento):
                sem_mascara += 1
            h.update(("%d %d %d %X %d\n" % (vb, th, inst, mask, lib)).encode())
    if ultimo_quadro != vblanks:
        prob.append("trace de progresso ate o VBlank %d de %d" % (ultimo_quadro, vblanks))
    if fins == 0:
        prob.append("nenhum episodio fechado da espera %08X" % espera)
    if sem_evento:
        prob.append("saidas sem evento: %d" % sem_evento)
    if outro_evento:
        prob.append("saidas liberadas por outro evento: %d" % outro_evento)
    if sem_mascara:
        prob.append("saidas sem o evento esperado no episodio: %d" % sem_mascara)
    if len(aberto) > 1:
        prob.append("episodios abertos no fim: %d" % len(aberto))
    for th, (vb, i) in aberto.items():
        if vb < vblanks - 1 or vb < ultimo_fim or depois.get(th):
            prob.append("episodio aberto (thread %d, VBlank %d, linha %d) sem a execucao terminar dentro dele" % (th, vb, i))
    r = resumo_espera(saida, espera)
    if r is None:
        prob.append("sem o resumo [progresso] da espera %08X" % espera)
    else:
        if r["episodios"] != episodios or r["com_evento"] != fins - sem_evento or r["sem_evento"] != sem_evento:
            prob.append("resumo do monitor (%d episodios, %d com evento, %d sem) != trace (%d, %d, %d)" %
                        (r["episodios"], r["com_evento"], r["sem_evento"], episodios, fins - sem_evento, sem_evento))
        if r["deadlock"]:
            prob.append("deadlock na espera: %d" % r["deadlock"])
        if r["sem_esperado"]:
            prob.append("saidas sem o evento esperado (monitor): %d" % r["sem_esperado"])
    res = {"episodios": episodios, "fechados": fins, "abertos_no_fim": len(aberto), "sem_evento": sem_evento,
           "outro_evento": outro_evento, "ultimo_quadro": ultimo_quadro, "assinatura": h.hexdigest()}
    return prob, res


def chamadas(linhas, vblanks):
    """Valida um trace de chamadas e devolve (problemas, linhas normalizadas sem seq nem strings, ultimo VBlank)."""
    if not linhas or linhas[0] != CAB_CALLS:
        return ["trace de chamadas sem cabecalho"], [], -1
    prob, out, ult = [], [], -1
    for i, l in enumerate(linhas[1:]):
        if l is None:
            prob.append("trace truncado na chamada %d (sem fim de linha)" % i)
            break
        f = l.split("\t")
        if len(f) != 11:
            prob.append("chamada %d com %d colunas" % (i, len(f)))
            break
        if f[0] != str(i):
            prob.append("seq %s na chamada %d (buraco ou fora de ordem)" % (f[0], i))
            break
        ult = max(ult, int(f[9]))
        out.append("\t".join(f[1:10]))
    if not out:
        prob.append("trace de chamadas vazio")
    elif ult != vblanks - 1:
        prob.append("trace de chamadas ate o VBlank %d de %d" % (ult, vblanks - 1))
    return prob, out, ult


def assinatura(seq):
    h = hashlib.sha256()
    for s in seq:
        h.update(s.encode() + b"\n")
    return h.hexdigest()


def s4n(leg, som, saida_leg, saida_som, vblanks, neg=None, saida_neg=None):
    prob = ["legado: " + x for x in execucao_completa(saida_leg, vblanks)]
    prob += ["sombra: " + x for x in execucao_completa(saida_som, vblanks)]
    pl, a, _ = chamadas(leg, vblanks)
    ps, b, _ = chamadas(som, vblanks)
    prob += ["legado: " + x for x in pl] + ["sombra: " + x for x in ps]
    if not pl and not ps and a != b:
        k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        prob.append("sequencia difere na chamada %d (legado %s | sombra %s); %d x %d chamadas" %
                    (k, a[k] if k < len(a) else "-", b[k] if k < len(b) else "-", len(a), len(b)))
    res = {"chamadas": len(b), "vblanks_com_chamadas": len({x.rsplit("\t", 1)[1] for x in b}), "assinatura": assinatura(b)}
    if neg is not None:
        pn = execucao_completa(saida_neg or "", vblanks)
        pn2, c, _ = chamadas(neg, vblanks)
        if pn or pn2:  # o negativo tambem precisa de execucao e trace completos (revisao Codex 2026-10-25)
            prob.append("negativo sem execucao ou trace validos: " + "; ".join(pn + pn2))
        elif c == b:
            prob.append("negativo (uma passada a menos por VBlank) nao detectado")
        elif len(c) >= len(b):
            prob.append("negativo com %d chamadas, nao menos que as %d do modo sombra (menos1 tira passadas)" % (len(c), len(b)))
        res["negativo_chamadas"] = len(c)
    return prob, res


def ler_linhas(path):
    """Linhas sem o \\n; a ultima vira None se o arquivo nao termina com \\n (escrita interrompida)."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace", newline="") as f:
            txt = f.read()
    except FileNotFoundError:
        return []
    if not txt:
        return []
    ls = txt.split("\n")
    if ls[-1] == "":
        ls.pop()
    else:
        ls[-1] = None
    return [x.rstrip("\r") if x is not None else None for x in ls]


def ler_texto(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def fechar(prob, res, a):
    if a.ref:
        with open(a.ref, encoding="utf-8") as f:
            ref = json.load(f)
        if ref.get("assinatura") != res.get("assinatura"):
            prob.append("assinatura difere da referencia pre-migracao (%s): %s x %s" %
                        (a.ref, ref.get("assinatura", "-")[:16], res.get("assinatura", "-")[:16]))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
    for p in prob:
        print("  " + p)
    print(("ok " if not prob else "FALHA ") + json.dumps(res))
    return 1 if prob else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("s2n")
    s.add_argument("--trace", required=True)
    s.add_argument("--saida", required=True)
    s.add_argument("--vblanks", type=int, required=True)
    q = sub.add_parser("s4n")
    for k in ("--legado", "--sombra", "--saida-legado", "--saida-sombra"):
        q.add_argument(k, required=True)
    q.add_argument("--vblanks", type=int, required=True)
    q.add_argument("--neg")
    q.add_argument("--saida-neg")
    t = sub.add_parser("s4seq")
    t.add_argument("--calls", required=True)
    t.add_argument("--saida", required=True)
    t.add_argument("--vblanks", type=int, required=True)
    for p in (s, q, t):
        p.add_argument("--json")
        p.add_argument("--ref")
    a = ap.parse_args(argv)
    if a.cmd == "s2n":
        prob, res = s2n(ler_linhas(a.trace), ler_texto(a.saida), a.vblanks)
    elif a.cmd == "s4n":
        neg = ler_linhas(a.neg) if a.neg else None
        prob, res = s4n(ler_linhas(a.legado), ler_linhas(a.sombra), ler_texto(a.saida_legado), ler_texto(a.saida_sombra),
                        a.vblanks, neg, ler_texto(a.saida_neg) if a.saida_neg else None)
    else:  # s4seq: uma execucao so (o experimental), para a referencia pre-migracao
        prob = execucao_completa(ler_texto(a.saida), a.vblanks)
        p2, seq, _ = chamadas(ler_linhas(a.calls), a.vblanks)
        prob += p2
        res = {"chamadas": len(seq), "assinatura": assinatura(seq)}
    return fechar(prob, res, a)


if __name__ == "__main__":
    sys.exit(main())
