"""Revisao das legendas pt-BR pelo Codex CLI (revisor independente, somente leitura), em lotes rastreaveis.

Le o pacote pt-BR gerado (fontes/traducao.pt-BR.tsv: chave, estado do ingles, ingles revisado, portugues), divide em
lotes com contexto (as falas vizinhas) e pede ao Codex UMA linha por chave: veredito (ok | corrigir | pendente),
sugestao, tipo e justificativa. Os dados vao no proprio prompt (o Codex nao precisa ler arquivos). Cada lote vira
reviews/legendas-pt-<data>/lote-NN.tsv e o resumo confere se todas as chaves voltaram. Nada e aplicado aqui: a decisao
fica em traducao.pt-BR.tsv, registrada no relatorio (aplicar_revisao.py).

    python tools/dev/legendas/revisar_codex.py [--lote 90] [--so 3,4] [--trabalho <pasta>]
"""
import argparse
import datetime
import os
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(AQUI)))
sys.path.insert(0, AQUI)
import gerar_mod as gm  # noqa: E402

REGRAS = """Voce e revisor independente de legendas (ingles -> portugues do Brasil) do jogo Blue Stinger (Dreamcast, 1999):
acao e terror numa ilha (Kimra/Dinosaur Island) atingida por um meteoro na noite de Natal. Personagens (pelas falas):
Eliot G. Ballade (jovem socorrista da ESER, de ferias), Dogs Bower (capitao do navio SS Deanna, mais velho; pai de
Janine), Janine King (seguranca da ilha, filha de Dogs e de Jean King), Nephilim (ser misterioso, chamado tambem de
"Neffy"/"Nephi"). Vozes de sistema (computador, alarmes) usam tom formal. As legendas sao exibidas em ate 2 linhas de 42
caracteres (a quebra e refeita automaticamente; nao precisa marcar \\n); os tempos sao os do ingles, entao a frase deve
caber no tempo da fala (prefira o conciso).

O ingles foi transcrito por IA e revisado por IA; linhas com estado_en "revisar" tem palavras duvidosas no ingles.

Glossario (manter exatamente): Eliot, Eliot G. Ballade, Dogs, Dogs Bower, Janine, Janine King, Jean King, ESER, Kimra,
Kimratech, Nephilim, Neffy, Nephi, Rat's Place (nome do bar, nao traduzir), SS Deanna, Hello Market, Lab Town, Deep Island,
Fossil Home, Dino-site, Disco Gigadent (Gigadent Disk), Dr. Jacob, Roland, Devlin, Chris, Tim, Ilha dos Dinossauros
(Dinosaur Island), "bonde" para shuttle (o bonde da rampa), "comporta" para shutter, "crachá" para ID card, Papai Noel
(Santa). Tratamento: "voce" entre os personagens.

Para CADA chave do lote (todas, sem excecao), responda uma linha TSV com 5 colunas, nesta ordem:
chave<TAB>veredito<TAB>sugestao<TAB>tipo<TAB>justificativa
- chave: exatamente como recebida (recurso linha), ex.: voz/e003 11
- veredito: ok (traducao boa), corrigir (erro comprovado pelo ingles: sentido, gramatica, ortografia, pontuacao, nome,
  consistencia, naturalidade claramente ruim, tamanho), pendente (depende de audio ou de contexto que nao existe aqui:
  ingles duvidoso, frase ambigua; NAO invente)
- sugestao: o texto portugues completo sugerido (vazio se ok); nunca invente conteudo que o ingles nao tem
- tipo: fidelidade | naturalidade | ortografia | gramatica | pontuacao | consistencia | glossario | tamanho | audio | -
- justificativa: curta, em portugues (vazio se ok)
Considere a continuidade com as falas vizinhas (uma frase pode continuar na linha seguinte). Nao escreva mais nada alem
das linhas TSV: sem cabecalho, sem comentarios, sem markdown."""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabalho", default=gm.TRABALHO)
    ap.add_argument("--lote", type=int, default=90)
    ap.add_argument("--so", default="", help="so estes lotes (1, 2, ...)")
    ap.add_argument("--saida", default="")
    a = ap.parse_args()
    linhas = gm.ler_tsv(os.path.join(a.trabalho, "mod", "legendas-pt-br", "fontes", "traducao.pt-BR.tsv"))
    if not linhas:
        raise SystemExit("rode antes o gerar_traducao.py")
    saida = a.saida or os.path.join(REPO, "reviews", "legendas-pt-" + datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    os.makedirs(saida, exist_ok=True)
    so = {int(x) for x in a.so.split(",") if x}
    lotes = [linhas[i:i + a.lote] for i in range(0, len(linhas), a.lote)]
    for n, lote in enumerate(lotes, 1):
        if so and n not in so:
            continue
        ini = (n - 1) * a.lote
        antes, depois = linhas[max(0, ini - 3):ini], linhas[ini + len(lote):ini + len(lote) + 3]
        fmt = lambda r: f"{r['recurso']} {r['linha']}\t{r['estado_en']}\t{r['en']}\t{r['pt']}"
        prompt = (REGRAS + f"\n\nLote {n} de {len(lotes)} ({len(lote)} chaves).\n"
                  "Contexto anterior (nao revisar):\n" + "\n".join(fmt(r) for r in antes) +
                  "\n\nLOTE (chave, estado_en, ingles, portugues):\n" + "\n".join(fmt(r) for r in lote) +
                  "\n\nContexto seguinte (nao revisar):\n" + "\n".join(fmt(r) for r in depois) + "\n")
        out = os.path.join(saida, f"lote-{n:02d}.tsv")
        r = subprocess.run(["codex", "exec", "--sandbox", "read-only", "--ephemeral", "-C", REPO, "-o", out, "-"],
                           input=prompt, text=True, encoding="utf-8", capture_output=True, shell=True)
        if r.returncode != 0 or not os.path.exists(out):
            print(f"lote {n}: codex falhou ({r.returncode}): {r.stderr[-400:]}")
            continue
        chaves = {f"{x['recurso']} {x['linha']}" for x in lote}
        vistas = {l.split("\t")[0].strip() for l in open(out, encoding="utf-8").read().splitlines() if "\t" in l}
        faltam = sorted(chaves - vistas)
        print(f"lote {n}: {len(chaves)} chaves, {len(chaves & vistas)} respondidas" + (f"; FALTAM {faltam[:8]}" if faltam else ""))
    print("saida:", saida)
    return 0


if __name__ == "__main__":
    sys.exit(main())
