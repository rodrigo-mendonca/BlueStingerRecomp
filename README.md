# Blue Stinger Recompiled

Recompilação estática (AOT) de **Blue Stinger** (Sega Dreamcast, SH-4) para
Windows x64 nativo. Não usa BIOS e não usa interpretador do SH-4 em produção.

> **Este repositório não contém o jogo.** Você precisa fornecer a sua própria
> cópia do disco. Ver [LEGAL.md](LEGAL.md).

Estado atual: **M1 concluído (extrator + verificação de hash)**. Ver [STATUS.md](STATUS.md) e
[ARCHITECTURE.md](ARCHITECTURE.md).

## Versão suportada

| Produto | Versão | Região | Status |
|---|---|---|---|
| T13001N | V1.000 (1999-07-05) | NTSC-U | alvo inicial |

## Fluxo (planejado)

```
bs-extract <seu_disco.cdi> game-data             # M1: extrai e valida o hash (pronto)
bs-recomp  config/bluestinger_us.toml             # M3: gera generated/*.cpp
cmake --build --preset clang-release              # compila BlueStinger.exe
```

## Extrair o disco (M1)

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

O teste `real_disc` usa a sua imagem se `BS_DISC_IMAGE` apontar para o `.cdi`.
Sem essa variável, ele é ignorado.

## Estrutura

```
config/       configuração TOML por versão do jogo (só metadados e endereços)
docs/         notas de hardware e engenharia reversa
tools/        extractor/ (bs-extract)
translator/   decoder/ analysis/ codegen/ app/ (bs-recomp)
runtime/      cpu/ memory/ hle/ gdrom/ pvr/ aica/ maple/ platform/ core/ game/ app/
tests/        unit/ (sem dados do jogo) e diff/ (diferencial contra o Flycast)
third_party/  dependências externas (adicionadas por marco)
```

Pastas locais ignoradas pelo git: `ISO/`, `game-data/`, `generated/`,
`analysis/`, `saves/`, `build/`.
