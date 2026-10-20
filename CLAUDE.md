# CLAUDE.md: Blue Stinger Recompiled

Recompilação estática (AOT) de Blue Stinger (Dreamcast, SH-4, NTSC-U T13001N
V1.000) para C++ nativo em Windows x64. Arquitetura e decisões em
`ARCHITECTURE.md`. O que já foi verificado, e como, fica em `STATUS.md`.

## Regras do projeto

- **Nunca versionar** imagem do jogo, BIOS, `game-data/`, `generated/`,
  `analysis/` nem `reviews/` (ver `LEGAL.md` e `.gitignore`).
- Trabalhar em etapas pequenas e testáveis. Antes de decisões grandes de
  arquitetura, apresentar opções e esperar a escolha do mantenedor.
- Licença MIT: não copiar código GPL (Flycast, N64ModernRuntime) nem de projetos
  sem licença. Usá-los só como referência de comportamento.
- `STATUS.md` registra cada verificação com data e método.
- Commits só quando o mantenedor pedir.

## Build e testes

Developer PowerShell do VS (ou `vcvars64.bat`):

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug                       # unitários (sem dados do jogo)
$env:BS_DISC_IMAGE="ISO/Blue Stinger.cdi"; ctest --preset msvc-debug   # + teste com a imagem real
```

Ferramentas:
- `bs-extract <img.cdi> game-data`: extrai e valida o hash. `bs-extract --runtime <img.cdi> <dir>`:
  dados de execução do jogo nativo (o jogo não lê o CDI), conferidos setor a setor.
- `bs-disasm game-data/1ST_READ.BIN 0x8C010000 <início> <n>`: disassembly.
- `bs-recomp config/bluestinger_us.toml [--analyze-only]`: análise + C++ em `generated/`.
- `tools/dev/compare_capstone.py`: decodificador vs. Capstone (venv opcional).
- `bs-builder --disc <img.cdi> --out <pasta> --kit . --sdl3 build/msvc-release/_deps/sdl3-src`:
  fluxo completo do Builder (CDI → BlueStinger.exe) a partir do repositório. Janelas sobre o mesmo núcleo:
  `BlueStinger-Recompiler.exe` (simples: idioma pt-BR/en-US, Modo portátil = `instalacao.toml`; `--auto
  --portatil --idioma <cod> --sem-filmes` para testes) e `BlueStinger-Developer.exe` (projeto C++, log técnico).
  Textos por chave em `builder/ui`; unitários `bs_ui_tests` (ctest `ui`).
- `tools/dist/make_preview.ps1 -Disc <img.cdi>` (pelo próprio Builder), `make_builder.ps1`: distribuições
  em `dist/`; testes `test_preview.ps1`, `test_gameplay.ps1` (NEW GAME até a 1ª área),
  `test_builder.ps1 -Disc <img.cdi>`, `test_projeto.ps1 -Disc <img.cdi>` (projeto editável e mods).
- Regressão completa retomável: `powershell -File tools/dev/regressao.ps1` (builds, unitários, ctest
  com e sem disco, Builder, Preview e todas as suítes em sequência). Pode ser interrompida; rodar de
  novo continua da primeira etapa não aprovada (estado em `%LOCALAPPDATA%\BlueStingerRecomp\regressao`;
  cada etapa tem a sua chave: código, Python, roteiros e produtores refazem tudo; editar só o script de uma
  suíte refaz só ela; uma regressão por vez, `ativo.txt` com a etapa em andamento e batimento a cada 30 s, limite de tempo
  por etapa com rc=124, durações no fim; `python tools/dev/tempos.py` mostra as etapas mais caras). `-Listar`
  mostra o estado, `-Novo` descarta, `-Desde <etapa>` refaz. Teste sintético do executor: `tools/dev/test_regressao.ps1`.
  Política (mantenedor, 2026-10-20, substitui a de 2026-10-18): **nível 1** (desenvolvimento) build
  incremental, unitários e testes ligados à mudança; **nível 2** (fechamento de N5, N6, N7) testes
  completos do subsistema, integração dirigida e comparação com o legado ou o Flycast, sem a regressão
  integral; **nível 3** (marcos críticos) regressão integral obrigatória antes da remoção do legado no N8,
  depois dela (com referências independentes) e na revisão final do N9. Mudança transversal com testes
  dirigidos insuficientes pode exigir uma regressão ampla antes; resultados aprovados e válidos pela chave
  não se repetem. Nunca enfraquecer validação para aprovar.
- Controles (N3-1): `controles.toml` na pasta do usuário (criado na 1ª execução); diagnóstico
  `BS_CONTROLE_VIRTUAL=<roteiro>` (controle virtual do SDL3, janela) com `BS_PAD_LEITURAS`;
  `test_controles.ps1 [-Casos K13] [-RotasK13 r1,r2]`. N3: `--controles nativo|legado` (padrão
  nativo; legado = fallback temporário pelo Maple), `BS_PAD_FECHAMENTO=<arq>` (prova de que servidor/decodificador do SDK e o Maple não atendem o
  controle), `BS_PAD_NATIVO_MUTACAO` (negativos), suíte `test_pd_nativo.ps1 [-Casos P1,...]`. Não rodar suítes que leem
  `dist\BlueStinger-Preview\data` junto com o `make_preview` (a troca da instalação é recusada).
- Diagnóstico do jogo: `bluestinger --headless N --dump-every K --capture <dir>` com
  `BS_INPUT_SCRIPT=tools/dist/roteiros/newgame_area1.txt`.
- Saves (N4): `--saves nativo|legado` (padrão nativo; pasta Documentos\DC-Recomp\Blue Stinger\saves,
  `--saves-pasta`, `--saves-portatil`; no `--headless` sem pasta, cartão temporário vazio por processo),
  `--saves-importar/--saves-exportar <vms>` [`--saves-nome`]. Suíte `test_saves.ps1 [-VmuFlycast <vmu> -PosFlycast x,y,z]`.
  Diagnósticos (build com mods): `BS_SAVES_H1=1` (SAVE pelo título) e `BS_SAVES_H2=<passo>` (simula só o
  save point no gameplay). Oráculo: `flycast_oracle.py bu-trace` (`--forcar-save-point`, `--ler-fim`), `n4_vmu.py`.
- Filmes (N5): `--video nativo|legado` (padrão nativo desde o fim do N5; legado = fallback temporário; nativo = MP4 do Builder em
  `data\video` + `mods\video`, Media Foundation/XAudio2) e `--legendas <idioma>`. O Builder converte os SFD
  (`--sem-filmes` desliga e apaga). `bs-video-probe [--builder <BSDATA> <saida> | --converter <dir> <SFD>]`;
  primeiro filme no jogo: `tools/dev/n5_e001.ps1`; matriz dos 17 IDs: `tools/dev/n5_matriz.py`; oráculo
  `tools/dev/n5_oraculo.py`. Unitários `bs_video_tests` (ctest `video`). Mods: `docs/MODS-VIDEO.md`.
- Modo espectador (janela): `BS_INPUT_SCRIPT=<roteiro>` sem `--headless` reproduz o roteiro na Preview
  SDL3 (só o roteiro controla; título com passo, sala e botões). Ex.: `$env:BS_RELOGIO_FIXO="1700000000";
  $env:BS_INPUT_SCRIPT="tools\dist\roteiros\passo_cgate.txt"; build\msvc-release\app\bluestinger.exe --data dist\BlueStinger-Preview\data`.
- Gameplay assistido (só desenvolvimento, `app/assist.h`): `python tools/dev/assistido.py iniciar <pasta>`
  e `... cmd <pasta> "pad UP" "passos 30" "quadro x"` (jogo pausado entre VBlanks, capturas PNG,
  RAM, sessão gravada como roteiro `indice passo`); `tools/dev/assistido_ir.py` navega até (x, z).
  Roteiros por passo de jogo valem igual nos dois modos de tempo: `tools/dist/test_assistido.ps1`
  compara `--tempo nativo` e `experimental` (quadros, leituras, áudio, estado por passo).

## Revisão com Codex (revisor independente)

O Claude é o responsável pelo desenvolvimento. O **Codex CLI** atua só como
revisor independente, em modo somente leitura.

**Quando revisar:** ao concluir uma etapa relevante, como um módulo, uma função
complexa ou um sub-marco (decodificador, semântica, analisador, gerador,
subsistemas do runtime). **Não** revisar a cada pequena edição.

**Como:**
```
powershell -File tools/dev/codex_review.ps1 -Scope "<o que foi feito e onde focar>" [-Base <ref>]
```
- O script verifica se o Codex está instalado e autenticado, monta o prompt
  (`tools/dev/codex_review_prompt.md`) com o diff e roda
  `codex exec --sandbox read-only --ephemeral`. O sandbox impede o Codex de
  modificar arquivos.
- O Codex tem acesso de leitura ao repositório, a `game-data/1ST_READ.BIN`, ao
  `bs-disasm` compilado e aos relatórios em `analysis/`.
- O resultado vai para `reviews/codex-<data>.md` (ignorado pelo git).
- Foco: equivalência funcional com o SH-4, ponteiros, memória, estruturas,
  lógica, chamadas de função e particularidades do Dreamcast.

**Depois da revisão:**
1. Analisar cada achado criticamente. Corrigir só o que for tecnicamente
   comprovado.
2. Em divergência entre Claude e Codex, decidir por evidência: bytes e
   disassembly do binário original, manual SH-4, testes (criar um teste que
   reproduza o caso sempre que possível).
3. Rodar os testes de novo depois das correções.
4. Registrar no `STATUS.md` a revisão, os achados aceitos e os rejeitados (com
   motivo).
5. Seguir o desenvolvimento.

Se o Codex não estiver disponível ou autenticado, registrar isso e seguir sem
revisão. Não instalar ferramentas nem mudar configurações globais sem
autorização do mantenedor.
