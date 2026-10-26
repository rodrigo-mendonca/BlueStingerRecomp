# Gera a lista de excecoes de SPC (formato do n8_referencias.py --excecoes) da cadencia B do N8.1 com a evidencia de
# CADA diferenca, conferida aqui (nada e aceito por classe):
#   - diferenca so de SPC, na mesma posicao, com o mesmo INTEVT, e o par (spc_ref -> spc_novo) e um dos provados pelo
#     disassembly (retorno do jsr do download da musica: 8C15DE72 no boot, 8C02A582 nas trocas de musica);
#   - a entrega pertence a uma espera do hardware: andando para tras, toda entrega ate a primeira fora da lista e
#     uma destas diferencas, e essa primeira (o inicio da espera) ja tinha o SPC novo na referencia;
#   - os eventos seguintes ate a proxima I/V (R = destino do rte desviado, T, J) sao iguais na referencia e na nova, e
#     nao ha T nem J entre eles (sem troca de thread nem longjmp);
#   - o primeiro estado gravado no VBlank da entrega ou depois (estado.tsv; o boot e o passo 0) igual;
#   - entre a primeira entrega da espera e esta, nenhum T, J nem N, e todas as entregas com origem `hardware` no
#     BS_INTC_TRACE da execucao nova (conferir com --ambiente BS_INTC_TRACE={pasta}/intc.tsv).
# O gerador nao compara o caso inteiro: isso e do n8_referencias.py, com a lista gerada.
# Qualquer outra diferenca aborta.
#   python tools/dev/n8_excecoes_cadencia.py analysis/n8/referencias <pasta do conferir> <sha do exe> > config/n8_1_excecoes_spc.toml
# A lista e o que o conferir aceita (--excecoes); conferir o arquivo gerado contra uma execucao do mesmo exe.
import os
import sys

ref, nova, exe = sys.argv[1], sys.argv[2], sys.argv[3]
RETORNOS = {"8C15DE72": "8C15DE6E: jsr @r3 (download da musica no boot), delay slot 8C15DE70",
            "8C02A582": "8C02A57E: jsr @r3 (download da musica nas trocas), delay slot 8C02A580"}
VELHOS = {"8C17DF0C": "retomada do BS_POLL_RET depois do jsr em 8C17DF08 (poll do quantum de ciclos dentro do tratador "
                      "anterior; dependia dos ciclos)",
          "8C1983D2": "retomada do BS_IRQ_CHECK do ldc r1,sr em 8C1983D0 (codigo aninhado)"}


def ev(p):
    out = []
    with open(p, encoding="utf-8") as f:
        for l in f:
            if l[:1].isdigit():
                c = l.rstrip("\n").split("\t")
                out.append((c[2], c[3], c[4]))
    return out


def tabela(p, chave, valor):
    d = {}
    with open(p, encoding="utf-8") as f:
        cab = f.readline().rstrip("\n").split("\t")
        for l in f:
            c = l.rstrip("\n").split("\t")
            d[c[chave]] = c[valor] if valor is not None else "\t".join(c)
    return cab, d


linhas = ["# N8.1, cadencia B (docs/N8-1-PREPARACAO.md): excecoes de SPC contra as 16 referencias do N8.0, uma por diferenca",
          "# (docs/N8-0-REFERENCIAS.md secao 5), com a evidencia de cada uma conferida por tools/dev/n8_excecoes_cadencia.py. APROVADA pelo mantenedor",
          "# em 2026-10-25 a solucao \"com correcao\" (a espera do hardware restaura o ponto de retomada antes de cada",
          "# atendimento). O SPC so passa pelo tratador 8C1982F4 em stc.l spc,@-r15 (8C19831C) e ldc.l @r15+,spc (8C19845A)",
          "# antes do rte em 8C198462; um rte desviado aparece como R no trace e e conferido abaixo. Nenhuma vale no estavel.", ""]
total = 0
for caso in sorted(os.listdir(nova)):
    a, b = os.path.join(ref, caso, "eventos.tsv"), os.path.join(nova, caso, "eventos.tsv")
    if not (os.path.exists(a) and os.path.exists(b)):
        continue
    ea, eb = ev(a), ev(b)
    if len(ea) != len(eb):
        raise SystemExit("%s: numero de eventos difere" % caso)
    difs = [i for i, (x, y) in enumerate(zip(ea, eb)) if x != y]
    if not difs:
        continue
    _, passo_de = tabela(os.path.join(nova, caso, "quadros.tsv"), 0, 3)  # vblank -> passo
    # origem de cada entrega (registro E, gravado logo antes do I: pos = numero de sequencia do I)
    origem = {}
    with open(os.path.join(nova, caso, "intc.tsv"), encoding="utf-8") as f:
        for l in f:
            c = l.rstrip("\n").split("\t")
            if len(c) == 13 and c[4] == "E":
                origem[int(c[0])] = c[5]
    _, est_r = tabela(os.path.join(ref, caso, "estado.tsv"), 0, None)
    _, est_n = tabela(os.path.join(nova, caso, "estado.tsv"), 0, None)
    vb, vb_de = None, []
    for x in ea:
        if x[0] == "V":
            vb = int(x[1], 16)
        vb_de.append(vb)
    for i in difs:
        x, y = ea[i], eb[i]
        if not (x[0] == y[0] == "I" and x[1] == y[1] and y[2] in RETORNOS and x[2] in VELHOS):
            raise SystemExit("%s: diferenca fora do provado: %s x %s" % (caso, x, y))
        v = vb_de[i]
        # a primeira entrega da mesma espera (na referencia) ja tem o SPC novo
        # (andando para tras, toda I entre a primeira entrega da espera e esta e uma destas diferencas: a mesma espera)
        # sem troca de thread, longjmp nem thread nova no caminho (revisao Codex 2026-10-25), e todas as entregas do
        # caminho feitas dentro da espera do hardware (origem E = hardware no BS_INTC_TRACE da execucao nova)
        dset, j = set(difs), i - 1
        while j >= 0 and not (ea[j][0] == "I" and j not in dset):
            if ea[j][0] in ("T", "J", "N"):
                raise SystemExit("%s: VBlank %d: %s entre a entrega e o inicio da espera" % (caso, v, ea[j]))
            if ea[j][0] == "I" and origem.get(j) != "hardware":
                raise SystemExit("%s: VBlank %d: entrega %d no caminho fora da espera do hardware" % (caso, v, j))
            j -= 1
        if j < 0 or ea[j][2] != y[2]:
            raise SystemExit("%s: VBlank %d: a entrega anterior fora da lista nao tem o SPC %s (%s)" % (caso, v, y[2], ea[j] if j >= 0 else None))
        if origem.get(i) != "hardware" or origem.get(j) != "hardware":
            raise SystemExit("%s: VBlank %d: entrega %d ou %d sem origem hardware no BS_INTC_TRACE" % (caso, v, i, j))
        k = i + 1
        seg_r, seg_n = [], []
        while k < len(ea) and ea[k][0] not in ("I", "V"):
            seg_r.append(ea[k]), seg_n.append(eb[k])
            k += 1
        if seg_r != seg_n or any(t[0] in ("T", "J") for t in seg_r):
            raise SystemExit("%s: VBlank %d: eventos seguintes diferem ou trocam de thread: %s x %s" % (caso, v, seg_r, seg_n))
        p = passo_de.get(str(v))
        # o primeiro estado gravado no VBlank da entrega ou depois (o boot e o passo 0, antes do primeiro estado)
        q = next((n for n in sorted(est_n, key=int) if int(est_n[n].split("	")[1]) >= v), None)
        if p is None or q is None or est_r.get(q) != est_n.get(q):
            raise SystemExit("%s: VBlank %d: passo %s sem estado igual depois (%s)" % (caso, v, p, q))
        segs = ", ".join("%s %s" % (t[0], t[1]) for t in seg_r) or "nenhum"
        total += 1
        linhas += ["[[spc]]", 'caso = "%s"' % caso, "vblank = %d" % v, 'intevt = "%s"' % x[1], 'spc_ref = "%s"' % x[2],
                   'spc_novo = "%s"' % y[2],
                   'justificativa = "entrega dentro da espera do hardware (timed_wait do slot nativo do download da musica): '
                   'o SPC da referencia, %s, era um c->pc velho: %s. O novo e o retorno da chamada (%s), o mesmo SPC da '
                   'primeira entrega desta espera na referencia (evento %d)"' % (x[2], VELHOS[x[2]], RETORNOS[y[2]], j),
                   'evidencia = "exe %s, evento %d: eventos seguintes ate a proxima I/V iguais na referencia e na nova (%s), '
                   'sem T nem J; esta entrega e a primeira da espera (evento %d) tem origem hardware no BS_INTC_TRACE, sem '
                   'T, J nem N entre elas; passo de jogo %s; estado do passo %s (o primeiro gravado no VBlank ou depois) '
                   'igual. O resto do caso (estado, saves, quadros, leituras, audio, legendas, cartao, relogios) e conferido '
                   'pelo n8_referencias.py com esta lista"' % (exe, i, segs, j, p, q), ""]
sys.stdout.write("\n".join(linhas))
sys.stderr.write("%d excecoes\n" % total)
