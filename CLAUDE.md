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
  fluxo completo do Builder (CDI → BlueStinger.exe) a partir do repositório.
- `tools/dist/make_preview.ps1 -Disc <img.cdi>` (pelo próprio Builder), `make_builder.ps1`: distribuições
  em `dist/`; testes `test_preview.ps1`, `test_gameplay.ps1` (NEW GAME até a 1ª área),
  `test_builder.ps1 -Disc <img.cdi>`, `test_projeto.ps1 -Disc <img.cdi>` (projeto editável e mods).
- Diagnóstico do jogo: `bluestinger --headless N --dump-every K --capture <dir>` com
  `BS_INPUT_SCRIPT=tools/dist/roteiros/newgame_area1.txt`.

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
