"""Legendas pt-BR: o pacote legendas-pt-br a partir do pacote ingles revisado e da traducao (traducao.pt-BR.tsv).

Os tempos, as chaves (voz/<nome> ou filme/<nome>, linha) e o que fica de fora vem do ingles (fontes/transcricoes.en-US.tsv
do pacote gerado por gerar_mod.py): a traducao so troca o texto. Cada linha e quebrada de novo em ate 2 linhas de 42
(as que nao cabem sao listadas para encurtar). Falta de traducao para uma linha que vai para o jogo e erro.

Saida: <trabalho>/mod/legendas-pt-br
  mod.toml, README.md
  legendas/falas.pt-BR.toml        as falas do gameplay
  legendas/<FILME>.pt-BR.srt       os filmes (SubtitleService)
  video/<FILME>.pt-br.srt          os filmes no leiaute do N5 (mods\\video, o player procura pt-br e depois pt)
  fontes/traducao.pt-BR.tsv        ingles e portugues lado a lado, com o estado da linha inglesa (base da revisao)

    python tools/dev/legendas/gerar_traducao.py [--trabalho <pasta>] [--versao 0.1.0]
"""
import argparse
import os
import shutil
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import gerar_mod as gm  # noqa: E402  (quebrar, ler_tsv, toml_str, srt_tempo, t_inicio, t_fim)

IDIOMA, ID = "pt-BR", "legendas-pt-br"


def texto_pt(t):
    t = t.replace("\\n", "\n")
    plano = " ".join(t.split())
    return gm.quebrar(plano) or t, gm.quebrar(plano) is not None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trabalho", default=gm.TRABALHO)
    ap.add_argument("--versao", default="0.1.0")
    a = ap.parse_args()
    en = gm.ler_tsv(os.path.join(a.trabalho, "mod", "legendas-en", "fontes", "transcricoes.en-US.tsv"))
    if not en:
        raise SystemExit("rode antes o gerar_mod.py (falta o pacote ingles)")
    pt = {(r["recurso"], int(r["linha"])): r["pt"] for r in gm.ler_tsv(os.path.join(AQUI, f"traducao.{IDIOMA}.tsv"))}
    mod = os.path.join(a.trabalho, "mod", ID)
    if os.path.isdir(mod):
        shutil.rmtree(mod)
    for d in ("legendas", "video", "fontes"):
        os.makedirs(os.path.join(mod, d))
    faltam, longas, falas, filmes, lado = [], [], [], {}, []
    for r in en:
        if r["estado"] == "excluida":
            continue
        chave = (r["recurso"], int(r["linha"]))
        if chave not in pt:
            faltam.append(f"{chave[0]} {chave[1]}")
            continue
        texto, cabe = texto_pt(pt[chave])
        if not cabe:
            longas.append(f"{chave[0]} {chave[1]}: {texto!r}")
        l = dict(inicio=float(r["inicio"]), fim=float(r["fim"]), texto=texto)
        lado.append([r["recurso"], r["linha"], r["estado"], r["texto"], texto.replace("\n", "\\n")])
        if r["recurso"].startswith("voz/"):
            falas.append((r["recurso"], r["id"], r["linha"], l))
        else:
            filmes.setdefault(r["arquivo"], []).append(l)
    if faltam:
        raise SystemExit(f"{len(faltam)} linha(s) sem traducao: " + ", ".join(faltam[:20]))
    with open(os.path.join(mod, "legendas", f"falas.{IDIOMA}.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"# Legendas em portugues (Brasil) das falas do Blue Stinger (pacote {ID} {a.versao}).\n"
                "# Traducao por IA do pacote legendas-en revisado, sem revisao humana; os tempos sao os do ingles.\n")
        for voz, vid, k, l in falas:
            o.write(f"\n[[fala]]  # id {vid}, linha {k}\nvoz = {gm.toml_str(voz)}\ninicio = {gm.t_inicio(l['inicio']):.2f}\n"
                    f"fim = {gm.t_fim(l['fim']):.2f}\ntexto = {gm.toml_str(l['texto'])}\n")
    for nome, ls in filmes.items():
        srt = "".join(f"{k}\n{gm.srt_tempo(l['inicio'])} --> {gm.srt_tempo(l['fim'])}\n{l['texto']}\n\n" for k, l in enumerate(ls, 1))
        for destino in (os.path.join(mod, "legendas", f"{nome}.{IDIOMA}.srt"), os.path.join(mod, "video", f"{nome}.pt-br.srt")):
            with open(destino, "w", encoding="utf-8", newline="\n") as o:
                o.write(srt)
    with open(os.path.join(mod, "fontes", f"traducao.{IDIOMA}.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("recurso\tlinha\testado_en\ten\tpt\n")
        for row in lado:
            o.write("\t".join(str(x).replace("\n", "\\n") for x in row) + "\n")
    # as linhas que a revisao (Codex + decisao) deixou pendentes de audio ou contexto: marcadas, nao alteradas
    pend = os.path.join(AQUI, f"pendentes.{IDIOMA}.tsv")
    n_pend = 0
    if os.path.exists(pend):
        shutil.copy(pend, os.path.join(mod, "fontes", f"pendentes.{IDIOMA}.tsv"))
        n_pend = max(0, len(open(pend, encoding="utf-8").read().splitlines()) - 1)
    with open(os.path.join(mod, "mod.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f'formato    = 1\nid         = "{ID}"\nnome       = "Legendas em português do Brasil (falas e filmes)"\n'
                f'versao     = "{a.versao}"\nprioridade = 10\nidiomas    = ["{IDIOMA}"]\njogo       = "T13001N V1.000"\n'
                'provisorio = true\n')
    with open(os.path.join(mod, "README.md"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"""# Legendas em português do Brasil para o Blue Stinger Native PC ({a.versao})

Tradução das legendas em inglês (pacote `legendas-en`, transcrição automática com revisão de texto por IA) para o
português do Brasil, feita por IA e **ainda não revisada por humanos**. Os tempos são os do inglês. Só texto: nenhum
áudio ou vídeo do jogo.

- `legendas/falas.{IDIOMA}.toml`: as falas do gameplay ({len(falas)} linhas).
- `legendas/<FILME>.{IDIOMA}.srt` e `video/<FILME>.pt-br.srt`: os filmes ({sum(len(v) for v in filmes.values())} linhas).
- `fontes/traducao.{IDIOMA}.tsv`: inglês e português lado a lado; as linhas cujo inglês ainda está `revisar` herdam a
  dúvida.
- `fontes/pendentes.{IDIOMA}.tsv`: {n_pend} linhas que a revisão por IA (Codex) deixou pendentes de áudio ou de
  contexto (não alteradas).

**Uso:** mod padrão do projeto (`mods-padrao/`), instalado pelo Builder em toda instalação. No jogo: Esc, Legendas,
Idioma `pt-BR`; pela linha de comando, `--legendas pt-BR`. Os nomes próprios seguem o texto do jogo (Eliot, Dogs,
Janine, Nephilim, ESER, Kimratech, Rat's Place).
""")
    print(f"{ID}: {len(falas)} falas, {sum(len(v) for v in filmes.values())} linhas de filmes em {len(filmes)} filmes")
    for l in longas:
        print("  nao cabe em 2x42:", l)
    return 0


if __name__ == "__main__":
    sys.exit(main())
