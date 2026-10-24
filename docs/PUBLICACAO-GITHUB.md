# Publicação no GitHub: verificação e limpeza do histórico

## Publicação de 2026-10-23 (autorizada pelo mantenedor)

`origin/master` estava em `aef3c0e`; o `master` local tinha **47 commits** depois dele, sem divergência (nada no remoto
que não estivesse no local). A limpeza reescreveu **só esses 47 commits**: `aef3c0e` e o que já estava publicado não
mudam, então o push é um avanço normal (sem force-push).

### Backup (antes de qualquer reescrita)

`%LOCALAPPDATA%\BlueStingerRecomp\backups\publicacao-20261023\`: `repo-completo.bundle` (`git bundle --all`, conferido
com `git bundle verify`), a lista dos refs, das worktrees e do stash, e cópias dos arquivos particulares
(`known_versions.h`, `docs/DISTRIBUICAO-COMPILADOR.md`, `mods-padrao/`, `icone-local/`, com os hashes conferidos). O
histórico anterior fica também no ramo **local** `backup/master-antes-publicacao` (`302cfa9`), que não é publicado.

### O que foi encontrado nos 47 commits

Varredura de todas as linhas adicionadas em cada commit contra as frases dos pacotes de legenda locais (en e pt-BR), mais
caminhos proibidos (imagem do jogo, BIOS, saves, `game-data/`, `generated/`, `analysis/`, `reviews/`, pacotes de
legenda, ícone da capa), segredos (tokens, chaves) e arquivos grandes:

| Commit | Achado |
|---|---|
| `2969913` | os pacotes `mods-padrao/legendas-en/` (transcrições das falas, `.srt` dos filmes, `falas.en-US.toml`) |
| `b1934df` | uma fala num unitário (`test_subtitle_overlay.cpp`) |
| `7c9d9f7` | duas falas e um nome de personagem num unitário (`test_localization.cpp`) |
| `8b73b3a` | um fragmento de fala num comentário de `tools/dev/legendas/sincronia.py` |
| `f62a1a9`, `b1934df` | citações de falas no `STATUS.md` |
| `075b43f`, `98206d3` | fragmentos de falas dos filmes em `sincronia.py` e `docs/LEGENDAS-PENDENCIAS.md` |

Nada de imagem do jogo, BIOS, saves, segredos ou arquivo maior que 2 MB.

### Reescrita (numa cópia do repositório)

`git filter-branch` numa clonagem local, só em `aef3c0e..master`:
- `--index-filter`: tira `mods-padrao/legendas-en` e `mods-padrao/legendas-pt-br` de todos os commits (os pacotes no
  disco continuam, ignorados pelo Git);
- `--tree-filter`: troca cada citação pelo texto neutro que os commits seguintes já usavam (ou por uma descrição);
- `--msg-filter`: tira o BOM do título de dois commits (os do menu).

Conferências: a nova varredura dá **0 achados** nos 47 commits; nenhum título com BOM; cada commit muda os mesmos
arquivos de antes (fora dos pacotes), salvo dois que só neutralizavam texto e ficaram sem essa parte; a árvore final só
difere da anterior nos três arquivos de texto saneados. O `master` local foi levado ao resultado com `git reset --keep`
(os dois arquivos particulares, com alteração local, ficaram intactos e conferidos pelo hash).

### Já publicados antes (não reescritos)

Três citações curtas já estavam em `origin/master` (`docs/N6-PLANO.md`, `docs/LEGENDAS-EN-PLANO.md`,
`tools/dev/legendas/test_legendas.py`). Tirá-las do histórico publicado exigiria force-push, que não foi autorizado:
foram trocadas por descrição ou texto próprio num commit novo. As outras ocorrências na varredura do histórico
publicado são falsos positivos de caminho (`translator/analysis/`, `runtime/hle/bios.*`) ou as versões públicas de
`known_versions.h` e `docs/DISTRIBUICAO-COMPILADOR.md` (as alterações particulares nunca foram commitadas).

### Regras que continuam

- Os pacotes de legenda (`mods-padrao/*/`) e o ícone da capa (`icone-local/`) ficam fora do Git; a distribuição local
  os leva (`make_builder.ps1 -LegendasLocais -IconeLocal`).
- Ramos `backup/*` são locais: não publicar.
- Antes de cada push: `python tools/dev/varre_publicacao.py origin/master..master` (sai com 1 se achar algo) e a
  conferência dos arquivos particulares.

## Publicação de 2026-10-24

O fechamento do N2 e a atualização objetiva do README foram publicados por avanço normal de `c92a31a` para
`0903ba5`, sem reescrever o histórico. Antes do push, `origin/master` era ancestral direto (`0` commits remotos
exclusivos e `8` locais); a varredura comparou 898 frases comerciais locais nos 8 commits e encontrou **0 achados**.
`known_versions.h`, `docs/DISTRIBUICAO-COMPILADOR.md`, saves, mods, backups, stashes e worktrees permaneceram locais.
