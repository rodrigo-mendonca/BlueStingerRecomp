# Publicação no GitHub: verificação e pendências

Situação em 2026-10-23. `master` está **33 commits à frente** de `origin/master` (aef3c0e); a verificação abaixo foi feita
sobre os 25 primeiros e os 8 seguintes (Vulkan padrão, N2, câmera, legendas) só acrescentam código, ferramentas e
documentação, sem dados do jogo (as legendas continuam fora do Git). Nenhum push foi feito; o
push depende da autorização do mantenedor.

## O que foi verificado nos commits locais

| Verificação | Resultado |
|---|---|
| Imagem do jogo, BIOS, `game-data/`, `generated/`, `analysis/`, `reviews/`, saves, capturas | nenhum arquivo nos commits |
| Executáveis e bibliotecas (`.exe`, `.dll`) | nenhum |
| Código de terceiros novo | só os cabeçalhos Vulkan-Headers 1.3.282 (Apache-2.0, licença em `third_party/vulkan`, registrado em `third_party/README.md`) |
| Maior arquivo | `third_party/vulkan/vulkan/vulkan_core.h` (1,0 MB) |
| Texto derivado das falas do jogo | **sim, no commit `2969913`** (ver abaixo); trechos de uma linha em `STATUS.md` e num unitário foram trocados por descrição ou texto próprio no fechamento do N7 |
| Pacotes de legendas no estado atual (`HEAD`) | fora do Git desde `b098f7b` (`mods-padrao/*/` ignorado; só `mods-padrao/README.md`) |
| Arquivos particulares do mantenedor | não commitados: `docs/DISTRIBUICAO-COMPILADOR.md` (alteração local), `tools/extractor/known_versions.h`, `_bs-extract.exe`, `tools/extractor/build/` |

## Bloqueio: o commit `2969913`

`2969913 N7c: legendas em ingles como mod padrao` adicionou `mods-padrao/legendas-en/` com o texto inglês 0.1.0
(transcrições das falas, `.srt` dos filmes, `falas.en-US.toml`). O `b098f7b` tirou esses arquivos do índice, mas eles
continuam no histórico: um push os publicaria. Os direitos de redistribuição **não existem** para esse texto
(`docs/LEGENDAS-PENDENCIAS.md`, seção "Direitos de redistribuição").

Também ficaram trechos curtos de fala em commits locais posteriores, já trocados no `HEAD`: duas frases de uma fala
num unitário (`tests/unit/test_localization.cpp`, commit `7c9d9f7`) e um fragmento num comentário de
`tools/dev/legendas/sincronia.py` (commit `8b73b3a`). Entram na mesma limpeza do histórico, se o mantenedor autorizar.

Há ainda trechos de uma linha já publicados em `origin/master` (`docs/LEGENDAS-EN-PLANO.md`, exemplos do erro de ASR)
e um no unitário `test_subtitle_overlay.cpp` do commit `b1934df` (trocado no `HEAD`). São citações curtas de exemplo,
não o pacote; ficam para a decisão do mantenedor.

### Procedimento proposto (NÃO executado; só com autorização do mantenedor)

Reescreve só os 25 commits locais (o que já está em `origin/master` não muda), tirando a pasta do pacote de todos eles:

```powershell
git branch backup/master-antes-publicacao master          # cópia do histórico atual
git filter-branch --index-filter `
  "git rm -r -q --cached --ignore-unmatch mods-padrao/legendas-en" -- origin/master..master
# conferências: nada do pacote nos commits a publicar, e a árvore final igual à de antes
git log --oneline origin/master..master -- mods-padrao/legendas-en          # vazio
git diff backup/master-antes-publicacao master --stat                       # vazio
```

Cuidados:
- a worktree `C:\Source\recomp-BStinger-n7` está no ramo `n7-vulkan`, que aponta para o mesmo commit do `master`:
  depois da reescrita, mover o `n7-vulkan` para o novo `master` (ou removê-lo) para ele não republicar o histórico
  antigo;
- `backup/master-antes-n7` e `backup/master-antes-publicacao` são ramos **locais**: não fazer push deles;
- o `filter-branch` deixa `refs/original/`; apagar só depois de conferir;
- os pacotes no disco (`mods-padrao/legendas-en`, `legendas-pt-br`) não são tocados (estão ignorados).

## Depois da reescrita

1. Conferir de novo: `git log --diff-filter=A --name-only origin/master..master` sem `mods-padrao/legendas-*`.
2. `git push origin master` (só `master`), com a autorização do mantenedor.
3. Legendas: publicar só as ferramentas e o processo (`tools/dev/legendas/`, `mods-padrao/README.md`); os pacotes
   continuam locais até haver autorização dos detentores dos direitos.
