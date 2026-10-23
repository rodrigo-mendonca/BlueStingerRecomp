"""Aplica as decisoes sobre a revisao do Codex das legendas pt-BR e escreve o relatorio rastreavel.

Entrada:
  <pasta>/lote-NN.tsv      respostas do Codex (revisar_codex.py): chave, veredito, sugestao, tipo, justificativa
  <pasta>/decisoes.tsv     decisao por chave (so as que nao sao "ok"): chave, decisao, texto, motivo
                           decisao = aceita (usa a sugestao) | ajustada (usa `texto`) | rejeitada | pendente
Saida:
  traducao.pt-BR.tsv       atualizado com as aceitas e ajustadas (copia anterior em <pasta>/traducao-antes.tsv)
  <pasta>/relatorio.md     uma linha por chave revisada nao-ok: anterior, sugestao, justificativa, decisao, final
  <pasta>/pendentes.tsv    as chaves que dependem de audio ou contexto (marcadas, nao alteradas)

    python tools/dev/legendas/aplicar_revisao.py <pasta>
"""
import glob
import os
import shutil
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
TRAD = os.path.join(AQUI, "traducao.pt-BR.tsv")


def tsv(p):
    return [l.split("\t") for l in open(p, encoding="utf-8").read().splitlines() if l.strip()]


def main():
    pasta = sys.argv[1]
    respostas = {}
    for p in sorted(glob.glob(os.path.join(pasta, "lote-*.tsv"))):
        for c in tsv(p):
            if len(c) >= 2:
                respostas[c[0].strip()] = (c + ["", "", "", ""])[:5]
    decis = {c[0].strip(): (c + ["", "", ""])[:4] for c in tsv(os.path.join(pasta, "decisoes.tsv"))[1:]}
    linhas = open(TRAD, encoding="utf-8").read().splitlines()
    cab, rows = linhas[0], [l.split("\t") for l in linhas[1:] if l.strip()]
    atual = {f"{r[0]} {r[1]}": r[2] for r in rows}
    naook = {k: v for k, v in respostas.items() if v[1].strip() != "ok"}
    faltam = sorted(k for k in naook if k not in decis)
    if faltam:
        raise SystemExit(f"{len(faltam)} chave(s) sem decisao: {faltam[:10]}")
    shutil.copy(TRAD, os.path.join(pasta, "traducao-antes.tsv"))
    novo, rel, pend = dict(atual), [], []
    cont = {"aceita": 0, "ajustada": 0, "rejeitada": 0, "pendente": 0}
    for k, (chave, veredito, sug, tipo, just) in sorted(naook.items()):
        _, dec, texto, motivo = decis[k]
        cont[dec] += 1
        if dec == "aceita":
            novo[k] = sug.replace("\n", "\\n")
        elif dec == "ajustada":
            novo[k] = texto
        elif dec == "pendente":
            pend.append([k, veredito, atual.get(k, ""), sug, just, motivo])
        rel.append(f"| `{k}` | {veredito} | {atual.get(k, '')} | {sug} | {tipo}: {just} | **{dec}**: {motivo} | {novo[k]} |")
    with open(TRAD, "w", encoding="utf-8", newline="\n") as o:
        o.write(cab + "\n")
        for r in rows:
            o.write(f"{r[0]}\t{r[1]}\t{novo[f'{r[0]} {r[1]}']}\n")
    total = len(respostas)
    with open(os.path.join(pasta, "relatorio.md"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"# Revisao do Codex das legendas pt-BR\n\nChaves respondidas: {total}; ok: {total - len(naook)}; "
                f"com observacao: {len(naook)} (aceitas {cont['aceita']}, ajustadas {cont['ajustada']}, rejeitadas "
                f"{cont['rejeitada']}, pendentes {cont['pendente']}).\nRevisao por IA (Codex) e decisao por IA (Claude): "
                "nenhuma revisao humana.\n\n| Chave | Veredito | Anterior | Sugestao | Justificativa | Decisao | Final |\n"
                "|---|---|---|---|---|---|---|\n" + "\n".join(rel) + "\n")
    with open(os.path.join(pasta, "pendentes.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("chave\tveredito\tatual\tsugestao\tjustificativa\tmotivo\n" + "".join("\t".join(p) + "\n" for p in pend))
    print(f"{total} respondidas, {len(naook)} com observacao: {cont}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
