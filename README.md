# Blue Stinger Recompiled

Recompilação estática (AOT) de **Blue Stinger** (Sega Dreamcast, 1999, SH-4) para um executável nativo de
**Windows x64**, em C++20. O código do jogo é traduzido instrução por instrução para C++ e os subsistemas do console
(arquivos, tempo, controles, saves, filmes, áudio, vídeo) são substituídos, um por vez, por implementações nativas do PC,
sempre provadas contra o jogo original rodando no emulador Flycast. Não usa BIOS nem interpretador do SH-4.

> **Este repositório não contém o jogo** nem nenhum dado dele. Você precisa fornecer a sua própria cópia do disco
> (CDI, NTSC-U T13001N V1.000). Ver [LEGAL.md](LEGAL.md).

## Status (2026-10-21)

**Progresso geral: ~64 %** do roteiro do port nativo (N0–N9; cada etapa pesa 10 %, e uma etapa parcial conta pela
fração concluída). O jogo roda no Windows a partir do disco do usuário, com arquivos, controles (teclado e controle
Xbox), saves e filmes nativos; o áudio nativo está em validação final.

| Etapa | O que é | Situação | % |
|---|---|---|---|
| M0–M4 | Extrator, decodificador SH-4, gerador de C++, runtime e primeiro executável | concluídas | — |
| N0 | Manifesto de hardware, fronteira jogo/SDK, interfaces nativas | concluída | 100 |
| N1 | Sistema de arquivos nativo | concluída | 100 |
| N2 | Tempo e eventos nativos | parcial (estável em uso; tempo das cargas de som e desempenho retomam depois do N7) | 50 |
| N3 | Controles nativos (SDL3/XInput, remapeamento, perfis) | concluída | 100 |
| N4 | Saves nativos (VMS/arquivos, importar/exportar) | concluída | 100 |
| N5 | Filmes nativos (Media Foundation, legendas por mod) | concluída | 100 |
| N6 | Áudio nativo (efeitos, falas, música Manatee, DSP da AICA) | efeitos e falas publicados; música, DSP e envelopes concluídos e validados contra o original em desenvolvimento; promoção a padrão pendente | 90 |
| N7 | Renderização Vulkan | não iniciada | 0 |
| N8 | Remoção da camada legada (simulação do console) | não iniciada | 0 |
| N9 | Revisão final e distribuição | não iniciada | 0 |

Detalhes e evidências de cada verificação: [STATUS.md](STATUS.md). Arquitetura: [ARCHITECTURE.md](ARCHITECTURE.md).
Planos por etapa em [docs/](docs/).

## Versão suportada

| Produto | Versão | Região | Status |
|---|---|---|---|
| T13001N | V1.000 (1999-07-05) | NTSC-U | alvo inicial |

## Como usar

O **Blue Stinger Builder** transforma o seu disco em um `BlueStinger.exe` com os dados de execução ao lado (nada do
disco sai da sua máquina):

```
bs-builder --disc <seu_disco.cdi> --out <pasta> --kit . --sdl3 build/msvc-release/_deps/sdl3-src
```

`BlueStinger-Recompiler.exe` faz o mesmo com uma janela simples (idioma, modo portátil). Ferramentas de linha de
comando para quem desenvolve:

```
bs-extract <seu_disco.cdi> game-data             # extrai e valida o hash
bs-recomp  config/bluestinger_us.toml             # gera generated/*.cpp a partir do executável do jogo
cmake --build --preset msvc-release               # compila o jogo
```

## Extrair o disco

Só imagens **CDI** por enquanto.

```
bs-extract --info "ISO/Blue Stinger.cdi"            # mostra faixas, IP.BIN e hash sem gravar nada
bs-extract "ISO/Blue Stinger.cdi" game-data         # extrai tudo + game-data/manifest.json
```

O executável `1ST_READ.BIN` precisa ter o SHA-256 de uma versão suportada
(`tools/extractor/known_versions.h`). `--force` extrai mesmo sem ter, e marca o
resultado como não verificado.

## Compilar

Requisitos: Visual Studio 2022+ (ou Build Tools) com CMake e Ninja, ou Clang.
Abra um *Developer PowerShell* e rode:

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Disassembly (ferramenta de desenvolvimento):

```
bs-disasm game-data/1ST_READ.BIN 0x8C010000 0x8C010000 64   # base, início, quantidade
bs-disasm --stats game-data/1ST_READ.BIN
```

O teste `real_disc` usa a sua imagem se `BS_DISC_IMAGE` apontar para o `.cdi`.
Sem essa variável, ele é ignorado.

## Estrutura

```
config/       configuração TOML por versão do jogo (só metadados e endereços)
docs/         notas de hardware e engenharia reversa
tools/        extractor/ (bs-extract), dev/ (scripts opcionais de verificação)
translator/   decoder/ (tabela SH-4 em sh4_isa.inc) e app/ (bs-recomp, bs-disasm)
runtime/      cpu/ memory/ hle/ gdrom/ pvr/ aica/ maple/ platform/ core/ game/ app/
tests/        unit/ (sem dados do jogo) e diff/ (diferencial contra o Flycast)
third_party/  dependências externas (adicionadas por marco)
```

Pastas locais ignoradas pelo git: `ISO/`, `game-data/`, `generated/`,
`analysis/`, `saves/`, `build/`.
