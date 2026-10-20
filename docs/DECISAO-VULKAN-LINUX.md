# Decisão: Vulkan como renderer principal e Linux como plataforma futura (2026-10-19)

Decisão do mantenedor. Substitui a D9 (Direct3D 11) da `ARCHITECTURE.md` §8 e amplia a D8
(plataforma). **Só documentação e contratos:** nada de Vulkan nem de Linux é implementado agora.
A ordem do roteiro não muda (seção 7).

## 1. A decisão

| Item | Antes | Agora |
|---|---|---|
| Renderer nativo (N7) | Direct3D 11 atrás de `IRenderDevice` | **Vulkan** atrás de `IRenderDevice`, nas duas plataformas |
| Plataforma | Windows x64 | **Windows x64**: desenvolvimento, validação e distribuição atuais. **Linux x86_64**: plataforma futura, sem port agora |
| Base | C++20, CMake, SDL3 | Igual, agora como requisito de portabilidade: C++20 e CMake portáveis; SDL3 para janela, entrada e integração com o sistema; recursos e lógica do jogo independentes do sistema operacional |

**Direct3D 11 no código:** não existe nenhuma implementação. A D9 só aparecia na documentação e
num comentário de `native/render/render.h`. Nada é removido nem quebra. A apresentação atual é a
do SDL3 (`SDL_Renderer`, que escolhe o backend), usada pelo caminho legado do PowerVR até o N7.

**Regra daqui em diante:** nenhuma dependência nova de Win32 no gameplay, no código recompilado
nem nos contratos compartilhados (`native/**.h`, `transitional/` fora dos adaptadores de
plataforma, `runtime/` fora das camadas de sistema). APIs de plataforma ficam em implementações
`.cpp` de backend, atrás de uma interface que já tenha uso concreto. Não criar implementação Linux
vazia nem camadas abstratas sem uso.

## 2. N7: renderer Vulkan (planejamento; implementação só no N7)

O contrato já existe e é neutro: `native/render/render.h`.
- **Captura:** a IR de comandos (`CommandList`) recebe o que hoje chega ao TA (store queues, DMA,
  escrita direta).
- **Estado de desenho nativo:** listas opaca, translúcida e punch-through, volumes modificadores,
  cor de deslocamento e fog por tabela. Sem registradores do PowerVR nem endereços de VRAM.
- **Dispositivo:** `IRenderDevice` (texturas, paleta, `submit`, `present_image`, `present`,
  `resize`).

Para o Vulkan, o N7 cobre:

| Requisito | Como entra no contrato |
|---|---|
| Gameplay recompilado, geometria, texturas e materiais | IR por quadro; texturas por `TextureHandle` (formatos do PVR convertidos no upload, cache por conteúdo) |
| Estados gráficos originais → nativos | enum de estado da IR → pipelines Vulkan (cache por combinação de estado); ordem e transparência do PVR (ordenação por lista e por pixel quando preciso) |
| Sincronização com o relógio lógico | o quadro é entregue no VBlank lógico (o mesmo ponto em que o host publica o quadro hoje); `present` não dita o ritmo do jogo |
| Framebuffers e composição de vídeo | o filme nativo (N5) entra como `present_image`/textura do quadro: `VideoFrame` ARGB em memória, sem textura ou superfície de API. Upload para imagem Vulkan no N7 |
| Resoluções modernas, escala e proporção | render target interno em múltiplos da resolução original; apresentação com escala e barras configuráveis; 4:3 por padrão |
| Instrumentação e comparação com o Flycast | captura do framebuffer final (como `BS_FRAME_LOG`/`--dump` hoje) e comparação por quadro com o Flycast e o caminho legado (PSNR/SSIM, como no N5) |
| Memória da GPU e recursos | vida útil por handle (`create`/`destroy`); destruição adiada até a GPU terminar o quadro que usa o recurso |
| Melhorias visuais e mods (N9) | shaders e texturas substituíveis por arquivo, como os filmes do N5 (manifesto e mods); o formato (SPIR-V pronto ou fonte compilada) é decidido no N7, sem antecipar o N9 |

**Fim do PowerVR emulado:** o N7 elimina a dependência permanente do PVR2. O caminho legado fica
só como referência de comparação enquanto o N7 não for aprovado.

**A decidir no planejamento do N7 (não agora):**
- carregador e alocador de memória (volk, VMA, ambos MIT, ou próprio);
- compilação de shaders (offline em SPIR-V no build ou no Builder);
- como o Builder distribui os shaders (kit de fontes ou SPIR-V pronto);
- a interação com o `SDL_Renderer` atual (o N7 cria a própria superfície Vulkan pelo SDL3:
  `SDL_Vulkan_CreateSurface`).

## 3. N5 e as interfaces de mídia

Media Foundation, XAudio2 e DirectWrite/Direct2D **ficam** como implementações Windows. Nada é
reescrito agora.

| Peça | Contrato hoje | Situação | Ação |
|---|---|---|---|
| Saída de áudio | `native/audio/audio.h`: `IAudioOutput` | interface de verdade; XAudio2 e saída nula são implementações (`audio_output.cpp`) | nenhuma |
| Decodificação de vídeo | `native/video/mf_movie.h` (`MfMovieReader`, cabeçalho sem tipos do Windows) | **concreto**: o `MoviePlayback` (ritmo, descarte do priming, fim) usa `MfMovieReader` direto | no fechamento do N5: interface fina de decodificador (abrir, formato, próximo quadro, próximo bloco de áudio) entre `MoviePlayback` e o Media Foundation. Sem mudar o comportamento |
| Legendas | `native/video/subtitles.h`: parser SRT/WebVTT portável; `SubtitleRenderer` concreto (DirectWrite/Direct2D/WIC no `.cpp`) | parser já portável; rasterização concreta | no fechamento do N5: interface de rasterização (texto → pixels sobre o quadro ARGB); DirectWrite fica como backend Windows |
| Catálogo, manifestos e mods de vídeo | `native/video/movie_catalog.h` (`std::filesystem`, toml++) | portável | nenhuma |
| Cola com o jogo | `transitional/video_mw_core`/`video_mw_glue` | portável (sem API de plataforma) | nenhuma |
| Conversão SFD → MP4 (Builder) | `builder/video` (pl_mpeg, ADX, Media Foundation SinkWriter) | demux e ADX portáveis; o codificador é Windows | ver seção 6 |

**Quadros de vídeo para o renderer:** o filme entrega `VideoFrame` (ARGB em memória). No N7, o
renderer Vulkan os recebe por `present_image` ou como textura. Não há dependência de superfície
Direct3D em lugar nenhum.

**Alternativas para Linux** (documentadas, **sem escolha agora**):
- **Saída de áudio:** o SDL3 já é dependência e tem saída de áudio (`SDL_AudioStream`).
  Candidato natural a `IAudioOutput` no Linux, sem biblioteca nova.
- **Decodificação:** depende do formato que o Builder gera. H.264/AAC exige um decodificador
  externo (FFmpeg/libav, LGPL; GStreamer, LGPL) com cuidado de licença e patentes. Um formato
  aberto gerado pelo próprio Builder (por exemplo AV1/VP9 + Opus, com decodificadores BSD:
  dav1d, libvpx, libopus) evitaria isso, ao custo de rever a D4 do N5 e a conversão.
- **Legendas:** FreeType + HarfBuzz (licenças permissivas) ou `SDL_ttf` sobre o mesmo parser.
- A decisão sai antes do port, com medida de fidelidade igual à do N5 (PSNR/SSIM, sincronia).

## 4. Arquivos e saves multiplataforma

O comportamento no Windows não muda:
- Documentos (`FOLDERID_Documents`, inclusive OneDrive) por padrão;
- modo portátil opcional (`--saves-portatil` ou `instalacao.toml`);
- pasta explícita (`--saves-pasta`) com prioridade maior.

**Contrato:** `native/platform/install_config.h` (na worktree do Recompiler) já é portável. A
função `saves_root` recebe a pasta de documentos como parâmetro e não chama API de sistema. A
resolução da pasta padrão fica no código de plataforma do executável (hoje
`SHGetKnownFolderPath` no `main_sdl.cpp`).

**No Linux** (a confirmar antes do port): saves em `$XDG_DATA_HOME/DC-Recomp/Blue Stinger/saves`
(padrão `~/.local/share/...`) e preferências (`controles.toml`, `recompiler.ini`) em
`$XDG_CONFIG_HOME` (padrão `~/.config/...`). O modo portátil e a pasta explícita valem igual.

**Gameplay:** a lógica do jogo e o código recompilado não têm caminho do Windows. Os caminhos
lógicos do disco (`BSDATA/...`) são do jogo; os físicos vêm de `data_files` e do executável.

## 5. Builder, Recompiler e Developer

Os dois executáveis Windows (worktree `../recomp-BStinger-ui`) seguem como estão.

O núcleo `bs_builder_core` é um pipeline só, chamado pelas duas janelas e pela CLI. Hoje ele
depende do Windows em três pontos, isolados em arquivos próprios:
- processos filhos (`child_process.cpp`, job objects);
- localização do compilador (`toolchain.cpp`: `vswhere`, `vcvars`, MSVC);
- troca de pastas e marcas (`builder_core.cpp`: `ReplaceFile`/`MoveFileEx`, `CreateFileW`).

O catálogo de textos (`builder/ui`) e o tipo dos erros (`ErrorKind`) já são portáveis. Uma
interface Linux futura reutilizaria o núcleo, trocando esses três pontos (GCC/Clang no lugar do
MSVC). A geração Windows continua prioritária.

## 6. Auditoria de dependências (2026-10-19)

### Bloqueantes para o port (exigem trabalho de projeto antes)

| Dependência | Onde | Por quê |
|---|---|---|
| Threads do guest em **fibras do Win32** (`ConvertThreadToFiberEx`, `SwitchToFiber`, `FIBER_FLAG_FLOAT_SWITCH`) | `runtime/cpu/sh4_threads.cpp`; teste de XMM com MASM (`tests/unit/xmm_probe.asm`) | o modelo de threads do guest inteiro depende delas. O Linux precisa de troca de contexto própria (ou ucontext), com os registradores preservados pela ABI System V |
| Toolchain e **build só com MSVC** | `CMakeLists.txt` (`if(MSVC)`, `/bigobj`, dependências por `/sourceDependencies` em `tools/build/msvc_srcdeps.cmake`); Builder (`toolchain.cpp`: `vswhere`/`vcvars`) | o Builder compila na máquina do usuário com o MSVC. No Linux: GCC/Clang. O código gerado é C++20 padrão (sem intrínsecos do MSVC encontrados), mas ainda não foi compilado com GCC/Clang |
| **Codificador de vídeo** do Builder (Media Foundation SinkWriter) e formato H.264/AAC | `builder/video/sfd_convert.cpp` (worktree N5) | sem Media Foundation não há conversão; o formato também decide o decodificador do jogo (seção 3) |

### Adaptáveis depois (trocar uma implementação atrás de interface existente ou pequena)

| Dependência | Onde | Caminho |
|---|---|---|
| XInput de reserva | `native/platform/input_system.cpp`, `runtime/input/input.cpp` | o SDL3 já é a entrada principal; no Linux, só o SDL3 |
| Escrita atômica dos saves (`ReplaceFileW`, `MoveFileExW`, write-through) | `native/platform/saves_store.cpp` | `rename` + `fsync` do arquivo e da pasta; a semântica de `.bak` e recuperação é a mesma |
| Pastas do usuário (`SHGetKnownFolderPath`, `%LOCALAPPDATA%`) | `app/main_sdl.cpp`, janelas do Builder | XDG (seção 4) |
| Write-watch do tempo experimental (`MEM_WRITE_WATCH`) | `transitional/cri_idle.cpp` (já em `#ifdef _WIN32`) | só no `--tempo experimental`: `mprotect` ou o *soft-dirty* do Linux; ou esse modo fica só no Windows |
| Media Foundation (leitura), XAudio2, DirectWrite/Direct2D/WIC | `native/video/*.cpp`, `native/audio/audio_output.cpp` (worktree N5) | interfaces da seção 3 (as de vídeo e legendas no fechamento do N5) |
| Janelas do Builder (Win32, common controls) e processos filhos | `builder/main_*.cpp`, `child_process.cpp` | interface Linux própria sobre o mesmo núcleo |
| Console, codificação de caminhos (`wchar_t` em caminhos do Builder) | Builder, `main_sdl.cpp` | `std::filesystem` já abstrai; trocar as conversões explícitas |

### Já multiplataforma

- Tradutor, análise e gerador (`translator/`), extrator e leitura de CDI (`tools/extractor`),
  `dcmap`.
- Código recompilado gerado (C++20; a confirmar com GCC/Clang).
- Núcleo `native/`: relógio e eventos, arquivos de dados, mapa de entrada (toml++), formato e
  envelope dos saves, `install_config`, catálogo de filmes, parser de legendas.
- Contratos `native/**.h`, entre eles `IRenderDevice` + IR e `IAudioOutput`.
- Cola de vídeo do N5 (`video_mw_core`, `video_mw_glue`).
- SDL3 (janela, entrada, apresentação), toml++, `pl_mpeg`.
- Catálogo de textos do Recompiler.

## 7. Ordem (inalterada)

1. Investigação e validação das suítes pendentes do N4.
2. Fechamento e consolidação do N4.
3. Integração do N5 e primeiro vídeo nativo (E001).
4. Conclusão do N5, com as duas interfaces da seção 3.
5. N6.
6. N7 com Vulkan.
7. Retorno às pendências do N2.
8. N8 e N9.

**Nada desta decisão antecipa o N6 ou o N7.** O CMake só ganha seleção de plataforma ou backend
quando houver um segundo backend real.
