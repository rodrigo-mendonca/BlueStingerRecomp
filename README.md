# Blue Stinger Recompiled

Recompilação estática (AOT) de **Blue Stinger** (Sega Dreamcast, 1999, SH-4) para um executável nativo de
**Windows x64**, em C++20. O código do jogo é traduzido instrução por instrução para C++ e os subsistemas do console
(arquivos, tempo, controles, saves, filmes, áudio, vídeo) são substituídos, um por vez, por implementações nativas do PC,
sempre provadas contra o jogo original rodando no emulador Flycast. Não usa BIOS nem interpretador do SH-4.

> **Este repositório não contém o jogo** nem nenhum dado dele. Você precisa fornecer a sua própria cópia do disco
> (CDI, NTSC-U T13001N V1.000). Ver [LEGAL.md](LEGAL.md).

## Status (2026-10-24)

**Progresso geral: ~72 %** do roteiro do port nativo (N0–N10, onze etapas de peso igual; uma etapa parcial conta pela
fração concluída; em 2026-10-22 o roteiro ganhou a N9, Remasterização, e os mods passaram para a N10). O jogo roda no Windows a partir do disco do usuário, com arquivos, controles (teclado e controle
Xbox), saves, filmes e **áudio nativos por padrão** (efeitos, falas, música Manatee e o DSP da AICA), e legendas em inglês
em português do Brasil por padrão, com inglês como alternativa (pacotes gerados localmente a partir do disco do usuário e instalados pelo Builder; transcrição, tradução e revisão por IA, provisórias; o texto não vai para o repositório). O desenho
é feito pelo **Vulkan por padrão** (N7 funcional, em fechamento técnico), com recuo automático para o renderizador em software.

| Etapa | O que é | Situação | % |
|---|---|---|---|
| M0–M4 | Extrator, decodificador SH-4, gerador de C++, runtime e primeiro executável | concluídas | — |
| N0 | Manifesto de hardware, fronteira jogo/SDK, interfaces nativas | concluída | 100 |
| N1 | Sistema de arquivos nativo | concluída | 100 |
| N2 | Tempo e eventos nativos | **concluída funcionalmente** (2026-10-24): U1–U8 comprovados; tempo lógico experimental promovido a padrão na janela, com `--tempo nativo` como recuo e o headless estável para preservar referências e gravações antigas. N2c (retirada de ciclos/INTC/`BS_POLL`) abre o N8 | 100 |
| N3 | Controles nativos (SDL3/XInput, remapeamento, perfis) | concluída | 100 |
| N4 | Saves nativos (VMS/arquivos, importar/exportar) | concluída | 100 |
| N5 | Filmes nativos (Media Foundation, legendas por mod) | concluída | 100 |
| N6 | Áudio nativo (efeitos, falas, música Manatee, DSP da AICA) | concluída (2026-10-23): nativo é o padrão (`--audio legado` é o fallback até o N8); validação contra o original (Flycast) e gravações reais de gameplay; H1–H9 aprovados, o H9 (tempo real sem faltas de dados) com o Vulkan padrão | 100 |
| N7 | Renderização Vulkan | **funcional, em fechamento técnico**: **o Vulkan é o renderizador padrão**, com recuo automático para o software (sem GPU compatível, ou se o dispositivo falhar no jogo); desenha o jogo **bit a bit igual** ao renderizador de referência, 3× mais rápido; **resolução interna de 720p a 4K**; **efeitos de imagem** (correção de cor, tonemapping, nitidez, bloom, CRT, shaders de mod); **menu de configurações dentro do jogo** (Esc ou Back do controle); **legendas** no gameplay e nos filmes; **câmera livre no analógico direito**. Bloqueios: a AMD integrada ainda apresenta `VK_ERROR_DEVICE_LOST` intermitente, agora confirmado como leitura inválida na GPU mas ainda sem o recurso culpado; a rota real usa fog por tabela e volumes modificadores a partir do render 1127, porém ambos continuam sem implementação fiel nem quadro pareado contra o Flycast. O fallback é mitigação, não correção. SSAO, 16:9 real e MSAA permanecem no N9 | 95 |
| N8 | Remoção da camada legada (simulação do console) | **planejamento técnico concluído; não iniciada**. N2c será a primeira subetapa; o inventário preserva ferramentas, saves, Builder/Recompiler e o fallback software. A remoção gráfica depende das evidências pendentes de fog/modifier volumes do N7 | 0 |
| N9 | Remasterização: duas edições instaláveis do mesmo núcleo, **Original** (fiel ao Dreamcast) e **Remastered** (câmera Livre obrigatória, menus e saves de PC, legendas ligadas, widescreen, iluminação, água, texturas HD, filmes e áudio remasterizáveis, acessibilidade); instalador gráfico. Planejamento em `docs/N9-PLANO.md` | não iniciada (planejada) | 0 |
| N10 | Sistema de mods (ferramentas, manifestos, pacotes de idioma, texturas, áudio, vídeo e shaders) e elevação dos sistemas do jogo para C++ editável; revisão final e distribuição | não iniciada (a arquitetura de extensão vem sendo preparada nas etapas anteriores) | 0 |

Desempenho: com o Vulkan (padrão) o gameplay leva ~7 ms por campo numa máquina de desenvolvimento (o tempo real é 16,7 ms) e o
áudio toca sem faltas; no recuo para o software (~17 ms por campo) o jogo pode ficar um pouco lento e o som pode picotar.

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
