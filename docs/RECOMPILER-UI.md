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

## Testes do Recompiler (`bs_ui_tests`, 16/16 na integracao de 2026-10-19; hoje 24/24 com o Developer)

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

## Dois produtos (decisão do mantenedor, 2026-10-20)

| | **BlueStinger-Recompiler.exe** (jogador) | **BlueStinger-Developer.exe** (desenvolvedores e modders) |
|---|---|---|
| Fluxo | CDI → Recompilar → Instalar → Jogar | três áreas: **Exportar projeto**, **Compilar projeto**, **Inspecionar recursos** |
| Opções | CDI (detectado na própria pasta), pasta, idioma, modo portátil (desmarcado) | pastas de projeto, recursos e instalação; Release/Debug; limpo ou incremental; sem filmes; log técnico |
| Mensagens | amigáveis por tipo de erro (`err.*`); o detalhe vai para o `builder.log` | etapa traduzida + detalhe técnico no status e no log |
| Textos | catálogo `builder/ui` (pt-BR, en-US), chaves gerais | o mesmo catálogo, chaves `dev.*` (72 por idioma) |
| Preferências | `recompiler.ini` | `builder.ini` (pastas, idioma, tipo, área) |
| Pipeline | `bs_builder_core` (o mesmo nos dois e no `bs-builder` da linha de comando) | idem; nenhuma lógica duplicada |

O Recompiler não mudou de comportamento: só passou a usar o header comum `builder/ui/win_shared.h` (chave
da etapa e idiomas do Windows), sem mudança de casos.

## BlueStinger-Developer (2026-10-20, worktree `../recomp-BStinger-dev`)

**O que o Developer exporta (e o que não exporta).** O projeto é uma **recompilação estática**: cada função do
`1ST_READ.BIN` traduzida do código de máquina SH-4 para C++20, instrução por instrução, mais o runtime e as
camadas nativas. **Não é o código-fonte original** nem uma descompilação legível:
- as funções têm nomes de endereço;
- operam sobre o contexto da CPU;
- estruturas, nomes e tipos não foram recuperados.

O README exportado (`LEIA-ME-PROJETO.md`) diz isso, documenta a estrutura real (`generated/`, `runtime/`,
`native/`, `transitional/`, `legacy/`, `app/`, `config/`, `tools/`, `mods/`, `third_party/`), o estado atual
(vídeo nativo; áudio ainda legado, N6), os requisitos (MSVC x64 pelo VS 2022 ou Build Tools; nada baixado) e o
aviso legal.

### Área *Exportar projeto*

Fluxo: **Validar CDI → Extrair e conferir os recursos → Recompilar (SH-4 → C++20) → Exportar**, sem compilador,
compilação nem instalação (`Request::exportOnly`, `bs-builder --somente-exportar`).
- Escolhe o CDI, a **pasta do projeto** e a **pasta dos recursos**. Os dados conferidos setor a setor ficam em
  `<recursos>\.bs-build\data`, e compilar depois com essa pasta dispensa o CDI.
- Opção sem filmes.
- O projeto leva `CMakePresets.json` (release e debug, MSVC x64, Ninja, mods ligados, dependências offline e a
  versão do Builder), o mesmo que o `compilar.cmd` usa, e `bs-projeto.toml` (versão e pasta dos recursos).
- Abre a IDE com CMake: Visual Studio (Abrir Pasta), VS Code (CMake Tools) ou CLion, com o kit do MSVC.
  **Só o CMake foi provado aqui** (`cmake --list-presets` e `cmake --preset release` no `test_projeto`); as
  IDEs não foram abertas.

### Área *Compilar projeto*

- Projeto (vazio: compila direto do CDI, o fluxo de antes) e pasta de instalação. Ao escolher o projeto, a
  pasta dos recursos do `bs-projeto.toml` vira a instalação.
- CDI só se os recursos ainda não estão preparados.
- **Release ou Debug** (`Request::buildType`; cada tipo com a sua pasta de build no cache). O Debug usa o CRT
  de depuração e roda só numa máquina com o Visual Studio.
- Limpo ou incremental, sem filmes.
- Compila com os mods, **verifica** o executável e instala (a anterior vai para `BlueStinger.anterior`);
  avisa antes de substituir, como o Recompiler. Abrir pasta e Jogar.

### Área *Inspecionar recursos* (sem o CDI)

`builder/resource_inspect`; aceita uma instalação ou um projeto exportado.
- **Instalação:**
  - `instalacao.toml`, lido como o jogo lê;
  - filmes pelo **mesmo catálogo do jogo** (`MovieCatalog`: Builder, mod, ausente, arquivo faltando, metadados,
    legendas);
  - áudio "sem manifesto ainda (N6)";
  - problemas: instalação incompleta, erros de manifesto.
- **Projeto:**
  - mapa de funções com a contagem por origem;
  - C++ gerado;
  - mods (C++, assets, vídeo);
  - presets;
  - recursos pelo **contrato do Builder**: a marca no formato atual dá "preparado, conferido ao compilar"; sem
    marca ou com formato antigo, "informe o CDI";
  - filmes como o jogo os resolveria com os mods do projeto.

### Linha de comando

`--disc --out --auto` continua compilando do CDI (o `test_builder` usa). Novos:
- `--exportar <projeto> --recursos <pasta>`;
- `--projeto <pasta>`;
- `--tipo Release|Debug`;
- `--inspecionar <pasta>`;
- `--idioma`, `--sem-filmes`.

No `bs-builder`, os novos são `--somente-exportar` e `--tipo`. Os comandos antigos continuam iguais.

### Revisão do Codex (`reviews/codex-20261020-135635.md`): 4 achados aceitos e corrigidos

| Gravidade | Achado | Correção | Teste |
|---|---|---|---|
| **ALTA** | O projeto **de origem** não era conferido contra `<saída>\.bs-build` e `<saída>\BlueStinger`: dentro deles, seria apagado (dados refeitos) ou trocado (instalação) | validação pelo canônico (resolve junções) para o projeto de origem e o exportado, antes de ler qualquer coisa | `dev_project_inside_builder_folders_refused`: 4 lugares × 2 modos, falha na validação, o arquivo do usuário fica |
| MÉDIA | Cancelar durante as cópias da exportação podia terminar em "exportado" | o token entra no `export_project` (entre as árvores) e é conferido antes do sucesso | `dev_export_version_and_cancel` |
| MÉDIA | A inspeção dizia "prontos" só porque o `data.ok` existia | o contrato do Builder (prefixo do formato) e o estado honesto "preparado, conferido ao compilar" | `dev_inspect_exported_project` (formato antigo recusado) |
| BAIXA | Os presets não levavam a versão do Builder | `BS_VERSION_OVERRIDE` no preset, com escape JSON | `dev_export_version_and_cancel` |

### Testes (2026-10-20, contra o Builder montado da worktree numa pasta isolada; o `dist\` compartilhado não foi tocado)

| Suíte | Resultado | O que prova |
|---|---|---|
| `bs_ui_tests` | **24/24** | catálogo completo nos dois idiomas; inspeção (instalação, incompleta, mods, manifestos, projeto); exportação (README, presets iguais ao `compilar.cmd`, `bs-projeto.toml`, mapa, reexportação preserva os mods e guarda o gerado editado); as 4 correções do Codex |
| `test_projeto` | **17/17**, 418 s | **só exportar em 13 s**, sem as etapas de compilação e instalação; presets aceitos e configurados offline; o projeto só exportado, editado, compila **sem o CDI** e a edição aparece no jogo; os 13 casos de antes (mods, reexportação, recompilar sem o CDI) |
| `test_builder` | **22/22**, 574 s | **Recompiler** (fluxo completo, modo portátil, ini do usuário preservado); Developer `--auto` e cancelamento (código 3, instalada intacta); erros, filmes, sem filmes |
| capturas | pt-BR e en-US | as três áreas; a inspeção da Preview real (19 recursos, 0 problemas) |

**Pendências:**
- abrir o projeto numa IDE de verdade (VS, VS Code, CLion): só o CMake foi provado;
- os indicadores do áudio na inspeção esperam o manifesto do N6;
- as ferramentas de tradução e edição são do N9 (`docs/LOCALIZACAO-PLANO.md`, na worktree do N6).
