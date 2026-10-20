# BlueStinger-Recompiler e BlueStinger-Developer (2026-10-19, worktree `../recomp-BStinger-ui`)

Pedido do mantenedor de 2026-10-19. Decisões dele:
- **dois executáveis** sobre o mesmo `bs_builder_core` (um pipeline só; o `bs-builder` CLI fica);
- implementação isolada durante a regressão do N4;
- **sem commit** desta funcionalidade sem nova autorização.

## O que existe

| Parte | Onde |
|---|---|
| `BlueStinger-Recompiler.exe`: tela simples | `builder/main_recompiler.cpp` |
| `BlueStinger-Developer.exe`: a antiga `BlueStingerBuilder.exe` | `builder/main_developer.cpp` |
| Catálogo por chave | `builder/ui/i18n.{h,cpp}`, `lang_pt_br.cpp` (referência), `lang_en_us.cpp` |
| Tipo do erro (`ErrorKind`) e `inspect_install` | `builder/builder_core.{h,cpp}` |
| Configuração da instalação | `native/platform/install_config.{h,cpp}` (`instalacao.toml`) |

**Recompiler:**
- Campos: idioma, imagem, pasta, **Modo portátil** (desmarcado por padrão, com o texto do lugar das
  partidas logo abaixo), Recompilar, Cancelar, Abrir pasta, Jogar, Ajuda.
- Progresso pelo nome traduzido da etapa.
- Mensagens amigáveis por tipo de erro: CDI inválido ou ausente, sem espaço, sem permissão,
  caminho longo, pasta ocupada, compilador, dados, compilação, teste, instalação. O detalhe
  técnico fica só no `builder.log`, indicado como "detalhes para diagnóstico".
- Avisos antes de tocar numa instalação:
  - existente (saves e mods preservados; a atual fica em `BlueStinger.anterior`);
  - pasta alheia;
  - troca de modo (as partidas não são transferidas e nada é apagado).
- Dicas (tooltips) e atalhos: F1 ajuda, Enter recompila, Esc cancela.
- Linha de comando para testes: `--portatil`, `--idioma <código>`, `--auto`.

**Idioma:**
- Lista suspensa (`CBS_DROPDOWNLIST`) preenchida por `ui::languages()`.
- Inicial: a preferência salva em `%LOCALAPPDATA%\BlueStingerRecomp\recompiler.ini`; senão o
  primeiro idioma preferencial do Windows, se houver tabela (`pt-BR`); senão `en-US`.
- A troca retraduz tudo na hora, inclusive o status já exibido (guardado como chave + parâmetros).
- Novo idioma: um `lang_<código>.cpp` e uma linha em `kLanguages`.

**`instalacao.toml`:**
- Gravado no pacote só quando a interface escolhe o modo (`Request::portableSaves`).
- Fica **fora da lista do pacote**: uma recompilação sem escolha (Developer, CLI) o preserva.
- Ordem de decisão da pasta: `--saves-pasta` > `--saves-portatil` > `instalacao.toml` >
  Documentos (`FOLDERID_Documents`, que segue o OneDrive).
- Caminho relativo ao executável: vale depois de mover a pasta.

## Testes (`bs_ui_tests`, 16/16)

- **Catálogo:** `ui_catalog_complete` (chave sem tradução ou sobrando, texto vazio, parâmetros
  `{n}` diferentes, chaves do `ErrorKind` presentes), `ui_languages_present`,
  `ui_translate_and_switch`, `ui_initial_language`.
- **Configuração:** `ui_install_config_modes`, `ui_install_saves_root` (inclusive outra
  unidade).
- **Troca de instalação:** `builder_install_swap_portable_config` (escolhido, preservado sem
  escolha, troca de modo sem tocar nos saves, pasta alheia).
- **Janela conferida em captura:** pt-BR, en-US e modo portátil.
- **`test_builder.ps1`, etapas 11 e 11b** (pesadas, rodam com a regressão livre): o Recompiler
  `--portatil --auto` grava o modo; o Developer recompila e o preserva.

## Integração (2026-10-20)

Integrado na árvore principal: o jogo lê o `instalacao.toml` (`app/main_sdl.cpp`, `savesRoot`), e os
testes de execução direta são os S15–S18 do `test_saves`. Registro, revisão do Codex e resultados no
`STATUS.md`, na seção "BlueStinger-Recompiler e BlueStinger-Developer". Bloqueio da distribuição (o
compilador) em `docs/DISTRIBUICAO-COMPILADOR.md`.
