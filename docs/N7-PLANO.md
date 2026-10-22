# N7 (renderização nativa Vulkan): plano e preparação (2026-10-20)

**Estado:** preparação técnica autorizada (investigação, documentação, testes sintéticos e protótipo
isolado), numa worktree própria (`../recomp-BStinger-n7`, branch `n7-vulkan`).

**Não autorizado:** integrar o Vulkan ao `BlueStinger.exe`, mudar o PVR2 atual, promover o renderer
ou começar a migração antes do fechamento do N6. Sem commit sem autorização.

Decisões que valem: Vulkan como renderer principal, Windows x64 e Linux x86_64 futuro
(`docs/DECISAO-VULKAN-LINUX.md`); N7a (IR de render) e N7b (provedores Ninja/Kamui)
(`docs/PLANO-PORT-NATIVO.md`).

## 1. Inventário do pipeline gráfico original

| Camada | O que se sabe (fonte) | Status |
|---|---|---|
| Jogo → **Ninja** (3D: matrizes, modelos `.NJ`, texturas `.NT`) | 32 entradas de fronteira e **528 pontos de chamada** do jogo; as mais chamadas são `8C16FE00` (104), `8C16FE48` (102), `8C1713A4` (52), `8C16FE54` (36) e `8C170DC8` (33) (`analysis/hwscan/fronteira.tsv`) | documentado (contagens); semântica por entrada **a investigar** |
| Jogo → outras entradas de render do SDK | 23 entradas de família ainda não identificada, 107 chamadas | **a investigar** |
| Ninja → **Kamui** (a camada do PVR: listas, texturas, estados) | Kamui: 79 funções; Ninja: 69 (`docs/N0-RELATORIO.md` §4.1) | documentado (família) |
| Kamui → **TA** (Tile Accelerator) | **o jogo nunca toca o TA**: todo envio vem do SDK por store queues (26,1 milhões de envios de 32 bytes em 8 000 VBlanks), DMA e escrita direta | documentado (N0) |
| TA → listas | parâmetros de 32/64 bytes por lista: opaca, modificador opaco, translúcida, modificador translúcido, punch-through (`runtime/pvr/ta.h`) | implementado no legado |
| Render (ISP/TSP) | rasterizador de **referência em software** (`runtime/pvr/pvr_render.cpp`): buffer de tile de 32 bits, profundidade em 1/w, fundo, ordem opaca → punch-through → translúcida | implementado no legado (~17 quadros/s em jogo) |
| Texturas | 1555/565/4444, paletas 4/8 bpp (paleta 1555/565/4444/8888), VQ 16 bits, twiddled e linear, mipmap (só o maior nível), YUV422 linear (`runtime/pvr/pvr_texture.cpp`) | implementado; **faltam** YUV422 twiddled, bump e paleta 4 bpp com mipmap |
| Transparência e ordenação | o jogo liga o *autosort* (`ISP_FEED_CFG = 0x00800408`, bit 0 = 0; 300 tiles em autosort na captura do Flycast; `STATUS.md`): translúcidas resolvidas **por pixel** (fragmentos por 1/w, do mais distante ao mais próximo, sem z-write, compare forçado `>=` contra os opacos) | **comprovado e implementado** na referência (`pvr_render.cpp`, teste `render_translucent_autosort_vs_presort`) |
| Profundidade, blending, fog, offset color, volumes modificadores | todos no estado de desenho da IR (`native/render/render.h`) | contrato pronto; semântica exata por modo **a medir** contra o Flycast |
| 2D, HUD, telas de carga | sprites do TA (listas normais) e framebuffer direto (FMV antigo, carregamento) | o filme nativo do N5 já entra como imagem ARGB |
| Sincronia | o quadro sai no VBlank lógico (o host publica o quadro do PVR hoje); `STARTRENDER` → fim de render (evento N2) | implementado (N2a/N2b); **o tempo não muda no N7** |
| Iluminação e transformação | no **SH-4** (Ninja: `ftrv`/`fsca`), não no PVR; chegam ao TA já em tela | por construção: o recompilado faz isso |

**Fronteira mais adequada:** em duas etapas, como no plano-mestre.
- **N7a, na saída do TA:** substitui o rasterizador em software por uma IR (`CommandList`)
  executada pelo Vulkan. Não muda nada da lógica (o jogo, o Ninja e o Kamui continuam), cobre
  geometria dinâmica, store queues, DMA e framebuffer direto, e o **rasterizador atual vira a
  referência de comparação**.
- **N7b, na fronteira Ninja/Kamui** (528 chamadas): provedores nativos sobre o mesmo renderer, com
  assets convertidos pelo Builder. Elimina o TA emulado.

**Sem emulação permanente de registradores:** a IR não tem registradores nem endereços de VRAM
(`render.h`). O caminho pelo TA no N7a é transitório, como a cola SH-4 dos outros N.

## 2. Fronteiras e dependências

```
jogo ──528──> Ninja ──> Kamui ──SQ/DMA──> TA (runtime/pvr/ta) ──> [N7a: IR CommandList] ──> IRenderDevice (Vulkan)
                                                                └─> pvr_render (software): referência e fallback
filme N5 (VideoFrame ARGB) ──────────────────────────────────────> IRenderDevice::present_image / textura
```

- **Já portável:** a IR e o `IRenderDevice` (`native/render/render.h`); a conversão de texturas e as
  regras da IR (`native/render`, esta preparação); o quadro do filme (N5) em ARGB, sem superfície de
  API gráfica.
- **Fica até o fim do N7b:** o TA emulado (só como produtor da IR no N7a) e o rasterizador em
  software (referência). Saem no N8 (`docs/PLANO-PORT-NATIVO.md`, seção 7).
- **SDL3:** janela, entrada e superfície Vulkan (`SDL_Vulkan_CreateSurface`, extensões da instância
  pelo SDL: Win32 hoje, Xlib/Wayland no Linux). Hoje a apresentação é o `SDL_Renderer`; no N7, a
  swapchain própria.

## 3. Arquitetura Vulkan proposta

| Parte | Desenho |
|---|---|
| Carregamento | Vulkan pelo SDL (`SDL_Vulkan_LoadLibrary` + `GetVkGetInstanceProcAddr`): sem a biblioteca de importação do SDK, igual no Linux. Cabeçalhos Vulkan (Apache-2.0) |
| Inicialização e encerramento | instância com as extensões do SDL; dispositivo com fila gráfica e apresentação (discreta primeiro); destruição na ordem inversa, contada (o protótipo termina com **0 objetos vivos**) |
| Swapchain e apresentação | FIFO (sempre disponível). **O ritmo continua o do relógio lógico**: o `present` nunca dita o tempo do jogo. Recriação em `OUT_OF_DATE`/`SUBOPTIMAL`/redimensionamento; janela minimizada pula a apresentação |
| Alvo do jogo | imagem offscreen na resolução interna (640×480 × escala), separada da swapchain; a apresentação copia com proporção 4:3 e barras (NEAREST em escala inteira) |
| Buffers | vértices e índices por quadro num anel (`CommandList` → buffer mapeado); sem alocação por desenho |
| Texturas e materiais | upload por `TextureHandle` com conversão PVR → RGBA8 (`texture_convert`); cache por conteúdo (o mesmo endereço de VRAM pode mudar de dados); paletas por *descriptor* |
| Estados | `DrawState` → *pipeline* (cache pela combinação de estado: blend, profundidade, cull, fog, `TexEnv`, punch-through) |
| Transparência | listas na ordem do PVR (`draw_order`). **Autosort por pixel é requisito** do backend fiel (OIT por pixel: listas de fragmentos por pixel ou *depth peeling*, com a mesma regra da referência). Ordenar por triângulo **não é equivalente** (primitivas que se cruzam) e só pode existir como modo não fiel |
| Volumes modificadores | cada `ModifierVolume` tem a sua lista (`OpaqueModifier`, lista 1 do TA, ou `TranslucentModifier`, lista 3); `modifier_order` dá os volumes de cada fase na ordem de submissão; no Vulkan, stencil |
| Composição 2D, HUD e vídeo | sprites como desenhos normais; o `VideoFrame` do N5 por `present_image` (textura de tela cheia) |
| Erros e perda do dispositivo | `VK_ERROR_DEVICE_LOST`: recriar dispositivo e recursos a partir dos dados da CPU (texturas e IR do quadro), sem afetar o jogo; outros erros, falha clara com diagnóstico |
| Captura determinística | o alvo offscreen lido por `vkCmdCopyImageToBuffer`; o protótipo dá o **mesmo SHA-256** em duas execuções, inclusive com redimensionamento no meio |
| Memória | alocação própria em blocos por tipo (poucas alocações), ou VMA (MIT) se a complexidade compensar; decisão na implementação |

Nada de Vulkan aparece no jogo, no código recompilado ou nos contratos: só atrás do `IRenderDevice`.

## 4. Shaders e texturas

- **Fonte editável em GLSL** (ou HLSL) no repositório; **SPIR-V** gerado no build.
- **Compilador:** resolvido em 2026-10-20 com o Vulkan SDK 1.4.363.0 isolado (seção 10): `glslangValidator` gera o SPIR-V no build (`find_program` pelo `VULKAN_SDK`), `spirv-val` e `spirv-opt` validam. Para o produto, a escolha entre exigir o SDK no build ou levar o `glslang` (BSD/Apache) como dependência fica para a integração.
- **No Builder e na distribuição:** os SPIR-V de referência vão prontos no kit (o usuário não compila
  shader). Shaders de mod podem vir em SPIR-V pronto, ou em fonte compilada na hora se o glslang
  entrar no produto: decisão no N9 (remasterização).
- **Texturas por mods** (N10, só preparado): a chave é o **conteúdo original** (hash do texel do PVR
  depois da conversão), não o endereço de VRAM, como nos mods de arquivo do N1. Uma substituição
  maior que a original muda só a amostragem (UV normalizada).
- **Resoluções:** resolução interna em múltiplos de 640×480; proporção original (4:3) como
  referência; widescreen futuro só onde a geometria permitir (N9, remasterização).
- **Filtragem:** a do jogo (por desenho) na referência; escala de apresentação NEAREST inteira ou
  linear.
- **Referência fiel:** escala 1×, 4:3, sem melhorias, é a configuração de comparação com o Flycast e
  com o rasterizador em software.

## 5. Testes de equivalência

| Nível | O quê | Tolerância |
|---|---|---|
| Unitários sem GPU (`bs_render_tests`, **5/5**) | texels 1555/565/4444, twiddle (bijeção, quadrada e retangular), conversão twiddled = linear, recusas; IR: validação com o desenho culpado e a ordem das listas | exato |
| Conversão = legado | `texture_convert` contra `pvr_texture` em todos os 65 536 valores e endereços (na integração; `native` não liga o legado) | exato |
| Vulkan × software | a mesma `CommandList` no Vulkan e no rasterizador de referência, por quadro, nas rotas do `test_assistido` | PSNR ≥ 40 dB e diferença máxima por pixel ≤ 2 nas bordas de triângulo. O raster de GPUs diferentes varia nas regras de cobertura e no arredondamento da interpolação, então a diferença é medida, não zerada |
| Vulkan × Flycast | capturas do framebuffer pelo oráculo (como o `n5_cor.py`), nos mesmos pontos lógicos | PSNR e SSIM medidos por cena; a referência fiel tem que ficar dentro do que o software já dá contra o Flycast |
| Composição | HUD sobre o 3D, vídeo do N5 em tela cheia, fundo, punch-through com limiar, fog por tabela, offset color | por caso, contra o software |
| Ciclo de vida e erros | criação e destruição contadas (0 vivos), recriação da swapchain, janela minimizada, recurso inválido recusado (`validate`), perda do dispositivo simulada | exato |

Os baselines não mudam para o teste passar. Diferença acima da tolerância é investigada (regra do
GPU contra o modelo do PVR) e documentada.

## 6. Riscos e questões em aberto

1. **Compilador de shaders** (seção 4): decisão e autorização.
2. **Autosort das translúcidas** do PVR2: o jogo usa (comprovado). O OIT por pixel custa memória e banda; o desenho (listas de fragmentos com limite por pixel, ou *depth peeling*) e o limite de camadas são decididos medindo as cenas mais densas contra a referência.
3. **Volumes modificadores** (sombras): o modelo do PVR (inclusão e exclusão por triângulo) precisa
   de stencil; validar contra o Flycast.
4. **Fog por tabela e offset color:** a equivalência exata com o PVR exige medir a tabela (como a
   cor do N5).
5. **Precisão de profundidade em 1/w:** o PVR usa ponto flutuante em 1/w; no Vulkan, Z invertido
   em float.
6. **Framebuffer direto** (telas de carga e FMV antigo): leitura da VRAM no N7a; no N7b, a origem
   nativa.
7. **Desempenho:** o objetivo do N7a é sair dos ~17 quadros/s; medir a montagem da IR na CPU.
8. **Linux:** o protótipo usa só o SDL3 e o carregador; o mesmo código deve abrir no Linux (a
   validar quando houver ambiente).

## 7. Protótipo independente (`tools/vk_prototipo`, fora do jogo)

Feito e funcionando sem o Vulkan SDK (cabeçalhos do SDL3, funções carregadas pelo SDL):

| Item do pedido | Resultado |
|---|---|
| Janela SDL3, instância, dispositivo | ok (NVIDIA GeForce RTX 5080, Vulkan 1.4) |
| Swapchain | ok; recriada 3 vezes na execução com redimensionamento |
| Textura sintética | ok (xadrez 128×128 sobre o vídeo, como um HUD) |
| Quadro de vídeo sintético | ok (320×240 ampliado 2× para o alvo 640×480, como o filme do N5) |
| Redimensionamento | ok (`--redimensionar`; proporção 4:3 com barras) |
| Encerramento | ok: **0 objetos Vulkan vivos** no fim |
| Captura determinística | ok: o mesmo SHA-256 em duas execuções; pixels conferidos contra o esperado |
| Sincronia | 1 quadro em voo: semáforo de aquisição único (o fence prova o fim da espera), semáforo de apresentação **por imagem da swapchain** (correção da revisão) |
| Swapchain robusta | tamanho limitado a `min/maxImageExtent`, alfa de composição entre os aceitos, formato escolhido só se aceitar `BLIT_DST`, blit LINEAR só se o formato tiver o filtro (correção da revisão) |
| **Triângulo** | **ok (2026-10-20, com o Vulkan SDK)**: strip pelos shaders `pvr.vert`/`pvr.frag` (SPIR-V gerado no build pelo `glslangValidator`; sem ele o protótipo compila sem o triângulo), textura amostrada com Modulate pela cor do vértice. Pixel (320,300) = (127,100,20) contra (128,100,20) calculado à mão (baricêntricas 1/3, texel aceso do xadrez); captura `e737856e…` igual em duas execuções; 0 objetos vivos |

## 8. Sequência de integração (depois do N6)

1. ~~Compilador de shaders e o triângulo no protótipo~~ (feito, seção 7); testes de estado (blend,
   profundidade, cull, punch-through) contra o rasterizador em software.
2. ~~**N7a: produtor da IR.**~~ (feito, seção 13) O TA atual passa a montar a `CommandList` além do
   render em software (transitório). `validate` em todo quadro durante a validação.
3. **Backend Vulkan atrás do `IRenderDevice`** no executável, por `--render nativo` (padrão
   software), com o quadro do N5 por `present_image`.
4. **Equivalência por rota:** software × Vulkan nas rotas do `test_assistido` e capturas do Flycast;
   desempenho por campo.
5. **N7b:** fronteira Ninja/Kamui. Análise das 32 entradas e das 23 desconhecidas, assets pelo
   Builder, e os provedores nativos.
6. **Promoção** a padrão e regressão; depois o retorno ao N2 (antes do N8), como no roteiro.

## 9. Revisão do Codex (2026-10-20, `reviews/codex-20261020-012348.md`)

Cinco achados, todos aceitos e corrigidos. Reexecução: `bs_render_tests` 5/5; protótipo com 120 quadros,
3 recriações, 0 objetos vivos, a mesma captura em duas execuções (SHA-256 `64343eda…2a38`).

| Achado | Decisão |
|---|---|
| [ALTA] semáforo de apresentação único reaproveitado sem prova de que o `present` anterior terminou | **aceito**: um semáforo por imagem da swapchain, criado e destruído com ela |
| [ALTA] o plano tratava o autosort como incerto, mas `STATUS.md` já o comprova | **aceito**: autosort por pixel virou requisito (seções 1, 3 e 6, `render_ir.h`); ordenação por triângulo só como modo não fiel |
| [MÉDIA] `imageExtent` sem limites, alfa de composição não conferido, `fmts[0]` sem teste de contagem | **aceito**: limites, escolha do alfa, contagem e resultados verificados |
| [MÉDIA] o formato da swapchain não era validado para `BLIT_DST`; LINEAR sem conferir o filtro | **aceito**: o formato só é escolhido se aceitar `BLIT_DST`; LINEAR só com `SAMPLED_IMAGE_FILTER_LINEAR` |
| [MÉDIA] a IR não associava volumes modificadores à lista opaca ou translúcida | **aceito**: `ModifierVolume::list`, validação e `modifier_order`; teste `render_ir_modifier_phases` |

Não verificado (sem Vulkan SDK): camadas de validação. Ficam para quando houver o SDK (seção 4).

## 10. Ferramentas e validação (2026-10-20)

**Vulkan SDK 1.4.363.0 (LunarG)**, instalado isolado em `%LOCALAPPDATA%\BlueStingerRecomp\sdk\VulkanSDK\1.4.363.0`
(só o componente core; instalador com SHA-256 igual ao do índice da LunarG e assinatura Authenticode válida
de "LunarG, Inc."). O instalador, mesmo com `--root`, mexe no ambiente global: `VULKAN_SDK`, `VK_SDK_PATH`
e PATH do sistema, 10 camadas explícitas no HKLM, `HKLM\SOFTWARE\LunarG` e atalhos. **Tudo isso foi
revertido** (instantâneo antes/depois idêntico). Ficou só o carregador do sistema (`vulkan-1.dll`)
atualizado para 1.4.363.0 pelo `VulkanRT` (compatível com versões anteriores; não é driver).

| Ferramenta | Versão |
|---|---|
| `glslangValidator` | 16.6.0 (Glslang 11) |
| `glslc` | shaderc v2026.4 |
| `spirv-val`, `spirv-opt`, `spirv-dis` | SPIRV-Tools v2026.4 |
| `VK_LAYER_KHRONOS_validation` (+ sincronização) | 1.4.363 |

Uso: `. "$env:LOCALAPPDATA\BlueStingerRecomp\sdk\vk-env.ps1" [-Validacao] [-Sincronizacao]` (variáveis só no
processo; desliga as camadas implícitas de terceiros: OBS, RTSS, overlays).

**Shaders** (`tools/vk_prototipo/shaders`): `pvr.vert` (vértice da IR em tela, 1/w em Z) e `pvr.frag` (TexEnv
do PVR, offset color e punch-through por constantes de especialização). Compilam com `glslangValidator`
(Vulkan 1.1), passam no `spirv-val`, e o `spirv-opt -O` também valida.

**Validation Layers: preparado, ainda não executado.** O terminal desta sessão roda **elevado** (nível de
integridade Alto), e o carregador do Vulkan ignora `VK_ADD_LAYER_PATH`/`VK_LAYER_PATH` em processo elevado
(confirmado com `VK_LOADER_DEBUG=layer`: nenhuma pasta explícita na busca). Duas saídas, a decidir:
1. rodar `tools/vk_prototipo/validar.ps1` num terminal **sem** elevação (nada global); ou
2. registrar só `VkLayer_khronos_validation.json` no HKLM (camada explícita: inerte se ninguém a pedir).

O `validar.ps1` cobre: V1 shaders (glslang → spirv-val → spirv-opt → spirv-val); V2 **controle negativo**
(`--controle-negativo` omite uma barreira: a camada TEM que acusar, prova de que está ativa); V3 120 quadros
com redimensionamento, validação completa e de sincronização com zero mensagens (inclui o semáforo de
apresentação por imagem, achado ALTA do Codex); V4 a mesma captura em duas execuções e 0 objetos vivos.
A ausência de objetos vivos sozinha não prova nada sobre a GPU.

**RenderDoc 1.46** (portátil, MIT), em `%LOCALAPPDATA%\BlueStingerRecomp\tools\RenderDoc_1.46_64`; assinatura
GPG válida da chave publicada em renderdoc.org (impressão `1B03 9DB9 A471 8A2D 699D E031 AC61 2C31 20C3 4695`).
O protótipo ganhou `--renderdoc-quadro N` (build com `-DRENDERDOC_DIR=<pasta>`; usa a API in-app só se o
RenderDoc já estiver injetado). A injeção funciona, mas **a captura Vulkan exige registrar a camada
implícita do RenderDoc no nível do sistema** (`renderdoccmd vulkanlayer --register --system`), o que é
mudança global: decisão do mantenedor. Depois disso: `renderdoccmd capture -w -c <pasta>\proto
build\vk\bs-vk-prototipo.exe --quadros 90 --renderdoc-quadro 60` e o `.rdc` abre no `qrenderdoc.exe`
(chamadas, texturas, buffers, shaders, blending e profundidade).

**Clang/MinGW:** o mesmo protótipo compilado com llvm-mingw dá a mesma captura do MSVC (SHA-256 `64343eda…`):
`../recomp-BStinger-tc/docs/PILOTO-LLVM-MINGW.md`.

## 11. Cenas sintéticas de equivalência (para quando houver o triângulo)

Dados de referência gerados pelo **rasterizador em software** (`runtime/pvr/pvr_render.cpp`) a partir de
`CommandList`s pequenas, comparados com o Vulkan na tolerância da seção 5:

| Cena | O que exercita |
|---|---|
| S1 dois triângulos translúcidos que se cruzam, submetidos na ordem errada | **autosort por pixel** (resolve na interseção; a ordenação por triângulo erra metade) |
| S2 três camadas translúcidas sobrepostas, alfas 0,25/0,5/0,75 | ordem de composição por 1/w e acúmulo de blending |
| S3 opaco + punch-through (limiar 0,5, alfa em degrau) + translúcido | ordem das listas (`draw_order`), `discard`, profundidade entre listas |
| S4 volume modificador opaco (inclusão e exclusão) sobre um opaco texturizado | stencil, `modifier_order`, as duas faces da regra do PVR |
| S5 volume na lista translúcida | fase `TranslucentModifier` |
| S6 texturas 1555/565/4444, paleta 4/8 bpp, VQ, twiddled e linear | `texture_convert` contra `pvr_texture` (exato) |
| S7 fog por tabela e offset color | a tabela de 128 entradas e a soma especular (medir contra o Flycast, como a cor do N5) |
| S8 HUD 2D sobre 3D e o `VideoFrame` do N5 em tela cheia | composição, `present_image`, proporção 4:3 |
| S9 profundidade: 1/w muito próximos e muito distantes | precisão do Z (float) contra o 1/w do PVR |

Cada cena vira uma `CommandList` serializada mais a imagem da referência em software (gerada no build, sem
dados do jogo), e o teste compara PSNR e diferença máxima. Ainda não implementado: depende do triângulo (seção 4).

**Pendência manual do mantenedor (decisão de 2026-10-20: sem registro no HKLM):** num PowerShell **sem
elevação** (abrir pelo menu Iniciar, não "Executar como administrador"):

```
cd C:\Source\recomp-BStinger-n7
powershell -NoProfile -ExecutionPolicy Bypass -File tools\vk_prototipo\validar.ps1
```

Comprovado só se: V2 mostrar a camada ativa **e** pelo menos uma mensagem `VUID-` do controle negativo, e
V3 terminar com zero erros e avisos relevantes (V1 e V4 também ok). Logs em `%TEMP%\bs_vk_validar`.
RenderDoc: registro temporário autorizado (só a camada do RenderDoc, com instantâneo e restauração), a
fazer com o Flycast fechado.

**Atualização (2026-10-20): validação pelo Git Bash**, sem PowerShell: `tools/vk_prototipo/validar.sh`, com os
mesmos casos do `validar.ps1` e o critério do V2 mais estrito (exige ao menos uma mensagem `VUID-`, além da camada
ativa no `VK_LOADER_DEBUG`). Recusa rodar com o Flycast ativo (código 3) e em processo elevado (código 2, detectado
pelo SID de integridade `S-1-16-12288`, independente do idioma). Logs separados em `%TEMP%\bs_vk_validar`:
`v1_shaders.txt`, `v2_negativo.txt` + `v2_negativo_camada.txt`, `v3_normal_1/2.txt` + `_camada.txt`.
Pendência do mantenedor, num Git Bash aberto **sem** "Executar como administrador", com o Flycast fechado:

```
cd /c/Source/recomp-BStinger-n7
bash tools/vk_prototipo/validar.sh
```

**RenderDoc: captura feita (2026-10-20, registro temporário autorizado, Flycast fechado).** Instantâneo de
`HKLM`/`WOW6432Node`/`HKCU ...\Khronos\Vulkan\{Implicit,Explicit}Layers` (14 entradas, mais `reg export`) em
`%LOCALAPPDATA%\BlueStingerRecomp\tools\renderdoc_registro`; `renderdoccmd vulkanlayer --register --system`
acrescentou só as duas entradas `renderdoc.json` (x64 e x86) nas camadas implícitas. Captura do quadro 61
(`--renderdoc-quadro 60`): `proto_frame61.rdc` (1,6 MB), convertido para XML com o `renderdoccmd convert`:
1 `vkCmdDraw` num `vkCmdBeginRenderPass` (o triângulo), pipeline, push constants e 2 shader modules, 2 blits
(vídeo e apresentação), 3 cópias buffer→imagem (vídeo, textura, HUD), 2 limpezas e o present, exatamente o
quadro do protótipo. As duas entradas foram removidas e o registro ficou **idêntico ao instantâneo**; o
`vulkanlayer --explain` volta a dizer "não registrada". Nenhum driver tocado.

## 12. Conversão de comandos, texturas e fidelidade (2026-10-20, durante a regressão do N5)

### Mapeamento: operação original → IR → Vulkan futuro

| Original (o que chega ao TA) | IR (`render.h`) | Vulkan (backend fiel) |
|---|---|---|
| Lista 0 (opaca), parâmetro global + faixas | `Draw` com `ListType::Opaque`, `DrawState` | passe opaco: pipeline por estado, profundidade 1/w (Z invertido em float), compare do desenho |
| Lista 4 (punch-through) | `ListType::PunchThrough`; `FrameParams::punch_through_ref` | depois dos opacos; `discard` com alfa < ref no fragment shader; >= forçado, frente para trás |
| Lista 2 (translúcida) com autosort | `ListType::Translucent` na ordem de submissão (`draw_order`) | **OIT por pixel** (listas de fragmentos ou depth peeling) com a regra da referência: do mais longe ao mais perto por 1/w, sem z-write, >= forçado contra os opacos |
| Listas 1/3 (volumes modificadores) | `ModifierVolume` (`modifier_order` por fase) | stencil por volume (inclusão/exclusão) e o segundo conjunto de parâmetros do polígono: **bloqueado** (abaixo) |
| ISP: compare, z-write, cull | `DrawState::depth`, `depth_write`, `cull` | estado do pipeline (compare op, write mask, cull mode; o "small" do PVR no shader) |
| TSP: blend src/dst | `Blend` (8 fatores) | blend fixo do pipeline (os 8 fatores do PVR têm equivalente direto, inclusive Other/InvOther = cor de destino) |
| TSP: filtro, wrap, flip/clamp | `Filter`, `Wrap` | sampler (cache por combinação) |
| TSP: shading instruction | `TexEnv` | constante de especialização do fragment shader (já no protótipo: `pvr.frag`) |
| Fog por tabela/vértice | `Fog`, `FrameParams::fog_table/fog_color` | tabela de 128 entradas num buffer uniforme; índice pela profundidade como o PVR (**a medir**) |
| Cor base e offset | `Vertex::color`, `offset`, `DrawState::offset_color` | atributos interpolados; `tex × base + offset` no shader |
| Transformação e iluminação | já em tela (feitas no SH-4 pelo Ninja recompilado) | nenhuma: vértices em pixels → NDC no vertex shader |
| Sprites/HUD | faixas na lista translúcida/opaca | desenhos normais |
| Framebuffer direto, filme (N5) | `present_image(argb)` | textura de tela cheia na apresentação |
| Registradores de controle (SOFTRESET, STARTRENDER etc.) | nenhum: a IR é o quadro inteiro | `submit` por quadro; **sem tradução de registradores em tempo de execução** |

### Texturas que o jogo realmente usa (varredura estática dos arquivos)

Foram encontrados **14 181 PVRT**, todos em `.NT`, com só 5 candidatos recusados por tamanho ou formato.

| Formato de pixel | Organização (quantidade) |
|---|---|
| RGB565 | retangular 4 123, twiddled 3 550, **VQ 2 553**, VQ pequeno + mip 58, twiddled retangular 10, twiddled + mip 5, VQ + mip 3 |
| ARGB4444 | twiddled 1 161, retangular 1 069, **VQ 467**, VQ pequeno + mip 59, **paleta 4 bits 14**, twiddled + mip 12, twiddled retangular 3, VQ + mip 1 |
| ARGB1555 | twiddled 472, retangular 340, **VQ 188**, VQ pequeno + mip 91, twiddled + mip 2 |

**Não aparecem nos arquivos:** YUV422, bump, ARGB8888, RGB555 e paleta de 8 bits. Tamanhos mais comuns:
64×64 (4 121), 128×128 (2 112), 32×32 (1 501).

**Ressalva:** texturas montadas em tempo de execução (render-to-texture, framebuffer) não aparecem nesta
varredura; a lista definitiva sai do oráculo e da integração.

**Conversores** (`native/render/texture_convert`), sem o legado:
- `convert16` (já existia): linear/retangular e twiddled;
- `convert_vq`: VQ normal (2 KB) e **VQ pequeno** com o codebook que o arquivo dá (`vq_codebook_entries`:
  o que sobra depois dos índices), com mipmap;
- `convert16_mips`: a cadeia inteira, do maior ao 1×1;
- `convert_pal4`: twiddled, índice par no nibble baixo.

**A conferir na integração:**
- o VQ pequeno contra o legado em VRAM (como o Kamui o carrega) e a paleta 4 bits com mipmap;
- **amostragem de mipmaps:** o legado só lê o maior nível; o hardware escolhe o nível. O modo fiel precisa
  medir contra o Flycast, e o conversor já entrega todos os níveis.

**Mods de textura (N10, preparado):**
- chave pelo hash do conteúdo convertido (seção 4); PNG (RGBA8) substitui por chave, de qualquer tamanho
  (UV normalizada);
- mipmaps do mod gerados no carregamento ou fornecidos;
- sem pacote completo agora.

### Cenas de fidelidade (`native/render/fidelity_scenes`, testes `test_render_fidelity.cpp`)

Cada cena traz a IR, as texturas, os pontos de prova com o valor esperado e a tolerância, a regra e a sua
fonte (os testes da referência em software). Também diz o modo: **fiel** ou **melhoria opcional**.
- **Fiéis:**
  - translúcidas que se cruzam (autosort por pixel);
  - profundidade >= com empate;
  - autosort força >=;
  - punch-through (limiar e frente para trás);
  - mistura One/One com saturação;
  - cor base + offset;
  - HUD translúcido;
  - quadro de vídeo pronto.
- **O autosort por pixel é discriminado:** a cena das translúcidas cruzadas tem uma prova que a ordenação por
  triângulo erra fora da tolerância. Essa ordenação só existe como cena de **melhoria** (não fiel).
- **Bloqueadas, com motivo e sem passar em silêncio:** volume modificador opaco e translúcido. A referência em
  software ainda descarta as listas 1/3, e a IR não tem o segundo conjunto de parâmetros do polígono ("dois
  volumes"). Antes de estender a IR, medir no oráculo se o jogo usa volumes e polígonos de dois volumes.

**Revisão do Codex** (`reviews/codex-20261020-112631.md`, só os componentes novos), 3 achados aceitos:

| Gravidade | Achado | Correção |
|---|---|---|
| **ALTA** | A cena de autosort **não discriminava** a ordenação por triângulo: cada quad são dois triângulos cujas médias de profundidade se invertem entre as metades, e a ordenação por triângulo acertava os dois lados (o valor "por triângulo" estava errado) | Cada camada cruzada é **um único triângulo** cobrindo as duas provas: qualquer chave única por triângulo (média, mínimo, máximo, submissão) dá uma só ordem e erra a prova da direita |
| MÉDIA | Os testes comparavam as constantes da própria cena: uma cena geometricamente incapaz de provar a regra passava | **Avaliador independente** no teste: cobertura e 1/w no centro do pixel, opacos com o compare do desenho, punch-through da frente para trás com o limiar, translúcidas com >= forçado, ordenadas por pixel (fiel) ou por triângulo (melhoria). Todas as provas saem da geometria, e a discriminação é calculada, não declarada |
| BAIXA | Os fixtures de VQ e paleta usavam o mesmo `twiddle_index` do conversor | Morton independente (espalhamento por máscaras) nos fixtures e um teste com valores calculados à mão, inclusive retangular nos dois sentidos |

Conferidos sem problema pelo Codex:
- codebook e sub-índice do VQ, índices em (w/2, h/2);
- limites de leitura dos conversores;
- deslocamentos de mipmap iguais ao `pvr_mip_offset`;
- `vq_codebook_entries`;
- os valores de punch-through, profundidade, One/One, offset e HUD;
- os volumes bloqueados.

`bs_render_tests` **14/14** (5 de texturas e 4 de fidelidade novos). Build só do alvo, `-j 2`.

### Portabilidade

Só C++20, `std::span` e a IR: nada de Win32 ou Vulkan no `bs_native_render`. O protótipo já usa só o SDL3 e
o carregador. A toolchain LLVM/MinGW deu a mesma captura (`docs/PILOTO-LLVM-MINGW.md`, worktree tc).

### Pendências que precisam do jogo ou do Flycast

- ~~uso real de volumes modificadores~~ (seção 13: o jogo usa muito, 76.290 volumes na rota de combate;
  dois volumes por polígono: nenhum, 0 parâmetros omitidos);
- seleção de mipmaps;
- fog por tabela;
- VQ pequeno em VRAM;
- texturas montadas em tempo de execução;
- as cenas no Vulkan contra a referência em software, que exige o backend.

## 13. N7a: captura do TA na IR (2026-10-21)

### Como funciona

- **Um decodificador só.** O `Parser` de `runtime/pvr/pvr_render.cpp` ganhou um observador
  (`PvrCaptureSink`, em `pvr_render.h`). Ele recebe:
  - cada vértice decodificado com o estado efetivo (`PvrTri`) e o fim de strip;
  - as quebras de strip;
  - os sprites (A, B, C, D);
  - os vértices de volume modificador com o global deles;
  - os parâmetros omitidos;
  - e, no fim, os triângulos desenhados e a imagem.
- Sem observador, o caminho é o mesmo de antes. Com e sem captura, os quadros da rota são idênticos à
  linha de base do executável do N6.
- **`transitional/render_capture`** monta a `CommandList`:
  - **Strip:** vira um `Draw` só enquanto o estado não muda. O k-ésimo triângulo usa os vértices
    k, k+1, k+2, com os dois primeiros trocados quando k é ímpar.
  - **Estado que muda no meio do strip** (o recorte do usuário entre vértices): daí até o fim do
    strip, um desenho por triângulo, na ordem exata.
  - **Sprite:** dois desenhos (A, B, C) e (A, C, D). Um strip de 4 só os reproduz numa ordem cíclica
    diferente, e isso muda as somas da interpolação em float.
  - **Volumes modificadores:** fecham pela instrução do global (ISP bits 30-29):
    - 0: polígono do volume;
    - 1: último polígono, inclusão;
    - 2: último polígono, exclusão.

    O global seguinte ao último polígono começa outro volume, mesmo que tenha as mesmas palavras. Volume
    sem último polígono é contado à parte ("sem fechamento").
  - **Parâmetros do quadro:** saem dos registradores (fundo, profundidade do fundo, fog, alfa de
    referência do punch-through, ordenação automática dos translúcidos, `HALF_OFFSET`,
    `FPU_CULL_VAL` e clamp de cor).
  - **`DrawState`:** passou a guardar também o recorte do usuário, *color clamp* e
    *ignore texture alpha*, porque o rasterizador os usa. Também guarda, para o backend, os bits que a
    referência ainda não usa: os quatro códigos de filtro (os dois trilineares são distintos), *mip
    bias*, *supersample* e SRC/DST select. O round-trip devolve todos os bits do TSP fora do tamanho, e
    os do ISP do estado. A única exceção é flip junto com clamp: o clamp prevalece, como no rasterizador.
  - **Culling:** 2 descarta o anti-horário na tela e 3 o horário (com y para baixo); os nomes no
    `Cull` foram corrigidos.
  - **Fog:** `FrameParams` guarda a tabela inteira (128 entradas de 16 bits), `FOG_COL_RAM`,
    `FOG_COL_VERT` e `FOG_DENSITY` em bruto, até a semântica do fog ser implementada.
- **`BS_RENDER_IR=capturar|conferir`** e **`BS_RENDER_IR_CADA=N`**: só diagnóstico. O padrão continua
  sem captura e com o renderizador em software.

### Prova de equivalência (modo conferir, em todo render)

Em cada render conferido, o modo conferir verifica:

1. `validate()` da IR e os handles de textura dentro da tabela do quadro. Se a IR for inválida, nada
   mais é percorrido.
2. Os `FrameParams` reproduzem os registradores de quadro que o rasterizador usou (`PvrFrame`:
   fundo, profundidade do fundo, `FPU_CULL_VAL`, stride, `PT_ALPHA_REF`, `HALF_OFFSET`, clamp e
   ordenação). O rasterizador agora lê esses registradores uma vez por render.
3. Os triângulos que a IR descreve (`pvr_state` + `pvr_vertex`, a volta exata do estado e das cores
   /255) são os que o rasterizador desenhou: os mesmos vértices bit a bit, o mesmo estado e a mesma
   textura, na mesma ordem.
4. A imagem redesenhada **só a partir da IR** (triângulos e `FrameParams`; `pvr_render_triangles`
   não lê registradores) é idêntica à do caminho do TA. Os texels ainda vêm da VRAM, e o item 5 prova
   que as conversões nativas os reproduzem.
5. Cada textura usada, convertida pelas conversões nativas a partir dos bytes da VRAM, é igual texel a
   texel ao `pvr_texel` do legado.

**Resultado** na rota `passo_combate` (9000 VBlanks, `--video legado`, relógio fixo, NEW GAME →
combate):

| Medida | Valor |
|---|---|
| Renders conferidos | 4487 de 4487 |
| Desenhos | 3.882.569 |
| Vértices | 32.720.376 |
| Triângulos | 18.625.730 |
| Sprites | 13.120 |
| IR inválida | **0** |
| Triângulos divergentes | **0** |
| Imagens divergentes | **0** |
| Texturas conferidas | 186.083 |
| Texturas divergentes | **0** |
| Texturas sem conversão nativa | 140 (paleta de 8 bits, YUV ou bump) |
| `FrameParams` divergentes | **0** |
| Volumes modificadores | 76.290 (2.109.836 triângulos), 1.173 sem último polígono |
| Parâmetros omitidos | 0 |

Os quadros (`BS_FRAME_LOG`) ficaram idênticos à linha de base, com e sem captura. Revisão do Codex
(`reviews/codex-20261021-220352.md`): 7 achados aceitos e corrigidos (STATUS.md).

**Achados:**

- **Vértices com 1/w NaN ou negativo.** O jogo envia esses vértices (1 render em 4487). O `validate()`
  passou a aceitar NaN e 1/w negativo, e só confere a estrutura. O backend segue a regra da
  referência (a profundidade decide), igual ao software.
- **Volumes modificadores.** O jogo os usa muito (sombras). O software ainda não os desenha, então o
  Vulkan também não os aplica na equivalência inicial. Aplicá-los fica para depois da equivalência,
  conferido contra o Flycast.

**Unitários** (`bs_unit_tests`, `test_render_capture.cpp`, pelo caminho do jogo: registradores, FIFO
do TA, STARTRENDER, modo conferir):

- strip → 1 desenho;
- sprite → 2 desenhos;
- recorte trocado no meio do strip → desenhos por triângulo;
- volta do estado e das cores;
- volume modificador;
- id de conteúdo independente do endereço.

### Texturas HD futuras: contratos (decisão do mantenedor, 2026-10-21)

**Escopo no N7.** Só os contratos e os pontos de extensão
(`native/render/texture_replacement.h/.cpp`, testes `test_render_replacement.cpp`). Não há
carregamento, upscale nem pacotes. As ferramentas, o carregador de mods e os pacotes ficam no N10.

**Identidade (`TextureId`).**
- É calculada pelo **conteúdo decodificado**: os texels ARGB8888 do maior nível mais a largura e a
  altura, com FNV-1a de 64 bits. Texto canônico: `<largura>x<altura>-<16 hex>`.
- Não depende do endereço na VRAM, do formato de armazenamento nem do banco de paleta.
- Na rota de combate, o jogo usou 249 chaves distintas (TCW + tamanho) para só **121 conteúdos
  distintos**. Uma chave por endereço duplicaria as substituições e quebraria quando a textura muda
  de lugar. `render_capture::content_id(TextureKey)` é o ponto de cálculo; o backend guarda o cache
  por `TextureId`.

**Regras de uma substituição** (o backend as aplicará quando houver provedor):
- mesma proporção e fator inteiro de 1 a 16 (`replacement_compatible`), para as UVs normalizadas do
  jogo não mudarem;
- o alfa vale como o original (punch-through, translúcidos, *ignore texture alpha*);
- os mipmaps são gerados da substituição;
- sem substituição, ou se ela for incompatível, vale sempre o original (fallback automático);
- a preferência é configurável, e o padrão é o original até o N9 (remasterização).

**Ponto de extensão.** `ITextureReplacements::find(TextureId)`. Sem provedor, só os originais.

**Formatos para o N10.**
- **PNG:** sem perda e simples.
- **KTX2:** mipmaps prontos e compressão de GPU (BC7/BC1, ASTC via Basis).
- O consumo de VRAM será medido, com orçamento e despejo no cache.
- HUD e interface entram pelo mesmo id (as texturas de HUD são texturas comuns da IR).

## 14. N7b: backend Vulkan (2026-10-21)

### Protótipo revisado antes de reutilizar

`tools/vk_prototipo` foi revisto e validado onde a máquina permite:
- **V1:** os shaders compilam e passam no `spirv-val`.
- **Execução funcional (RTX 5080):** 120 quadros com 3 recriações da swapchain; captura idêntica em
  duas execuções (SHA-256 `E737856E…`); 0 objetos Vulkan vivos no fim.
- **V2/V3 (Validation Layers):** **pendentes.** O terminal desta sessão é elevado (integridade alta) e
  o carregador ignora as camadas por variável. A tentativa de rebaixar o token para integridade
  média foi bloqueada pela política de segurança da sessão.
  - `validar.ps1` passou a testar a integridade do processo (a regra do carregador), não o grupo de
    administradores.
  - Basta rodá-lo num terminal comum (não elevado).

**O que foi reaproveitado:** o ciclo de vida (instância → dispositivo → recursos → destruição contada)
e a leitura de volta. **O que não foi:** os shaders do protótipo, que cobriam pouco do estado do PVR.

### Desenho

| Peça | Onde | O que faz |
|---|---|---|
| Preparação do quadro | `native/render/frame_prep` (portável, sem GPU) | Faz na CPU o que o rasterizador de referência decide por triângulo, com as mesmas contas: strips → triângulos na ordem do TA, culling (pequeno, anti-horário, horário), área nula ou não finita, 1/w não finito, recorte "dentro" vazio, cor do último vértice no plano, fases (opaca, punch-through, translúcida) e lotes por estado. A profundidade é o próprio 1/w vezes uma **potência de 2** do quadro (exata: a igualdade continua igual) |
| Dispositivo | `native/render/vulkan/vk_device` (`VulkanDevice : IRenderDevice`) | Offscreen na resolução do quadro. Cor RGBA8, profundidade D32 com *depthClamp* (o PVR não recorta por z), pipelines por estado fixo (modo, comparação e escrita de z, fatores de mistura), texturas BGRA8 com a amostragem feita no shader, leitura de volta. O Vulkan é aberto em tempo de execução (`vulkan-1.dll`/`libvulkan.so.1`): sem SDK nem biblioteca de importação. Sem GPU ou recurso exigido, `create()` falha com o motivo |
| Shaders | `native/render/vulkan/shaders` (GLSL); SPIR-V versionado em `spirv/` (`tools/dev/gera_spirv.ps1 [-Conferir]`) | O Builder e o usuário não compilam shader |
| Integração | `transitional/render_capture` | Ganchos `PvrCaptureSink::render` e `--render nativo`; detalhes abaixo |

**Shaders**, mesmas regras do `Raster::draw`:
- `pvr.vert`: atributos sem perspectiva do hardware, multiplicados por 1/w e divididos no fragmento.
  É a correção de perspectiva do software.
- `pvr.frag`: amostragem manual (`texelFetch`) com wrap, clamp e flip, ponto ou bilinear e meio
  texel; sombreamento (decal, modulate, decal alpha, modulate alpha); deslocamento; *use alpha*;
  *ignore texture alpha*; *color clamp*; limiar do punch-through (alfa quantizado); recorte "fora".
  A cor é quantizada como o `pack` do software.
- Variante `AUTOSORT` de `pvr.frag`: *early fragment tests* com >= contra as opacas; o fragmento vai
  para a lista do pixel.
- `resolve.comp`: mistura cada pixel do mais distante para o mais próximo, com empate pela ordem de
  submissão. Usa os fatores de cada fragmento, quantiza a cada passo e não tem limite de camadas
  (seleção repetida do próximo). Se a lista não couber, o mesmo quadro é refeito com mais espaço;
  nunca se perde fragmento.

**Integração:**
- `--render nativo` (ou `BS_RENDER=nativo`): a imagem do Vulkan substitui a do software. A gravação
  no framebuffer, os filmes, a apresentação e as capturas não mudam.
- `BS_RENDER_IR=comparar`: o software continua como saída e o Vulkan é comparado pixel a pixel. Com
  `BS_RENDER_IR_BMP=<pasta>`, grava software | Vulkan | diferença ×8.
- **Recuo:** sem Vulkan, o jogo fica no software, com o motivo no log e no resumo.
- **Texturas:** cache pelos bytes de origem na VRAM e pela paleta (refeita só quando mudam). Sem
  conversão nativa (paleta de 8 bits, YUV, bump), passa pelo decodificador do legado texel a texel,
  contada e **nunca descartada**. Texturas sem uso há 600 renders saem da GPU.

**Volumes modificadores:** continuam na IR, contados, sem efeito na imagem, como no software. A
aplicação (stencil por volume, inclusão e exclusão por lista) entra na seção 16, comparada com o
Flycast.

**Padrão:** o software continua o padrão (`kRenderNativeDefault = false`) até a aprovação do
mantenedor.

### Testes dirigidos

- `bs_render_tests`:
  - `frame_prep_*` (4 casos): ordem dos strips, culling, fases, sombreamento plano, escala exata,
    recorte e amostra no canto.
  - `render_vulkan_fidelity_scenes`: as **8 cenas fiéis** de `fidelity_scenes` (17 provas) no
    Vulkan, todas dentro da tolerância, cada uma duas vezes com a mesma imagem, e 0 objetos vivos no
    fim. Sem Vulkan: [skip]; com `BS_EXIGIR_DADOS=1`, falha.

### Modo fiel: as contas do software na GPU

A primeira versão usava a interpolação e a rasterização da GPU. Ela ficava visualmente igual, mas não
bit a bit:
- 1,04 % dos pixels diferentes;
- PSNR médio de 64 dB;
- 144 quadros abaixo de 40 dB, todos com fatias finas ou texels na fronteira, porque a GPU encaixa
  os vértices em 1/256 de pixel e interpola com a sua própria aritmética.

O **modo fiel** (resolução original) refaz no shader, por pixel, as contas do `Raster::draw` a partir
dos dados de cada triângulo (`frame_prep`: posições originais, 1/w, UV, cores e `1/area` calculado na
CPU como o software):
- funções de aresta no ponto de amostra e a regra de cima/esquerda;
- pesos e 1/w;
- pesos na amostra dos atributos (`HALF_OFFSET` bit 1);
- correção de perspectiva com **divisão corretamente arredondada** (`crdiv`, um passo de FMA);
- o mesmo `c/255` dos bytes;
- as mesmas somas na mesma ordem.

Soma e produto são corretamente arredondados no Vulkan, e `precise` impede fusão e reassociação.

Para cobertura e profundidade:
- a **rasterização conservadora** (`VK_EXT_conservative_rasterization`, quando existe) entrega todos
  os pixels candidatos e a cobertura final é a do shader;
- a profundidade é o 1/w do software (`gl_FragDepth`);
- o autosort usa um **segundo subpasso** que lê a profundidade como anexo de entrada e faz o `>=` com
  o 1/w do software.

Sem `F_EXACT` (as resoluções maiores do N7c), vale a interpolação da GPU.

### Resultados (rota `passo_combate`, 9000 VBlanks, relógio fixo; 2026-10-22, versão final do N7b)

| Medida | NVIDIA RTX 5080 | AMD Radeon integrada |
|---|---|---|
| Garantias do modo fiel (escolhidas pela GPU) | arredondamento ao par, profundidade crua; subnormais no padrão do dispositivo | arredondamento ao par, subnormais preservados, profundidade crua |
| `comparar` (software como saída; Vulkan comparado em todo render) | **4487/4487 bit a bit iguais** | 4486/4487; 1 recriação do dispositivo (pendência abaixo) |
| `--render nativo` (o Vulkan como saída) | `BS_FRAME_LOG` **idêntico ao do software** nos 9000 VBlanks; lógica do jogo idêntica; determinístico | idêntico; 0 falhas em 2 rotas completas |
| Tempo da rota (headless; software: 224 s) | **69 s** | 102 s |
| GPU por quadro | média 1,48 ms, pior 12,7 ms | média 8,6 ms, pior 28 ms |
| Camadas translúcidas por pixel no jogo | até 78 | até 78 |
| Texturas | 281 envios; 12 pelo decodificador do legado; 181 descartadas do cache por desuso | igual |

**Desempenho.** A leitura de volta em memória do host sem cache (combinada para escrita) custava
cerca de 65 ms por quadro: a rota levou 514 s, contra 66 s com `HOST_CACHED`.

**Autosort (`resolve.comp`).**
- **Até 128 camadas** (o jogo chega a 78): ordenação por inserção num vetor local.
- **Acima de 128:** seleção repetida do próximo.
- Passadas de 64 em 64 davam imagem errada na AMD integrada, então foram abandonadas.
- Testes de 70 e 140 camadas, idênticos nas duas GPUs.

**Pendência de estabilidade (AMD integrada, só no modo `comparar`):**
- **Sintoma:** `VK_ERROR_DEVICE_LOST` no envio do render 371 em quase todas as rotas. O render 370
  sai com a imagem anterior, embora a cerca e a fila ociosa acusem sucesso.
- **Isolamento:**
  - o render gravado (`BS_RENDER_IR_GUARDAR`) e reproduzido sozinho dá na AMD a mesma imagem da
    NVIDIA (50 repetições);
  - não aparece no modo nativo, com atraso entre quadros nem com o software sequencial;
  - não há TDR no log do sistema.
- **Mitigação:** recuperação (dispositivo recriado; o render é refeito; até 3 por sessão), e a rota
  completa com 1 quadro errado.
- **Próximo passo:** as Validation Layers num terminal não elevado.

**Testes dirigidos** (nas duas GPUs):
- `bs_render_tests`, 24 testes: `frame_prep` (5) e as 8 cenas fiéis no Vulkan, determinísticas, com 0
  objetos vivos.
- `bs_unit_tests`, 285 testes:
  - `render_capture_vulkan_full_image_matches_software`: imagem inteira Vulkan × software em 12
    cenas, todas **idênticas**:
    - gouraud, fatias finas, sprite, recorte, textura bilinear com espelho;
    - `HALF_OFFSET` 1 e 3, 1/w negativo, o contraexemplo de profundidade da revisão;
    - autosort com 6, 70 e 140 camadas.
  - Falhas injetadas: textura, dispositivo transitório (recuperado) e permanente (software).
  - Substituição HD.
  - Reprodução de um render gravado (diagnóstico).

**Critério de equivalência (seção 5), corrigido.** "Diferença ≤ 2 nas bordas" não fazia sentido: uma
borda coberta por triângulos diferentes difere pela cor inteira. O critério passa a ser:
- no modo fiel, **imagem idêntica** ao software, o que a rota de combate cumpre na NVIDIA;
- nos modos de melhoria (N7c), PSNR ≥ 40 dB, medido.

### Revisões do Codex

- **1ª rodada** (`reviews/codex-20261021-231515.md`): 6 achados, todos aceitos; ver STATUS.md.
- **2ª rodada** (`reviews/codex-20261022-000105.md`): 5 achados, todos aceitos (a divisão inteira, as
  variantes de ambiente FP, o retângulo envolvente, a profundidade crua, os testes de igualdade); ver
  STATUS.md.

### Pendências do N7b

- **Validation Layers (V2/V3)** num terminal não elevado (`tools/vk_prototipo/validar.ps1` e o
  backend, com `VK_INSTANCE_LAYERS`).
- **Rotas além da de combate**, por exemplo as do `test_assistido`.
- **Comparação com o Flycast**, no oráculo gráfico.
- **Fog e volumes modificadores** (seção 16), que o software não aplica: a referência para eles é o
  Flycast.
- **Apresentação direta pelo Vulkan** (swapchain própria, sem passar pelo framebuffer emulado):
  pré-requisito das resoluções maiores do N7c.
- **Aprovação do mantenedor** para o Vulkan virar padrão.

## 15. N7c: configurações gráficas modernas (depois da equivalência do N7b)

Ordem obrigatória: só depois de o Vulkan estar aprovado na resolução original. Cada opção tem o
"Original" (Dreamcast) como referência, e nenhuma muda o ritmo do jogo. O FPS não depende da
resolução: o ritmo continua o do relógio lógico. 60 FPS fixos ou FPS ilimitado dependem do N2.

| Grupo | Opções | Notas de implementação |
|---|---|---|
| Resolução interna | original (640×480), 720p, 1080p, 1440p, 4K; escala configurável | o alvo offscreen em escala × 640×480; vértices × escala; recortes e tesoura × escala; o 1/w não muda |
| Apresentação | janela, tela cheia, janela sem bordas; V-Sync | swapchain própria do Vulkan (FIFO com V-Sync; MAILBOX/IMMEDIATE sem), separada do ritmo do jogo |
| Proporção | 4:3 (original), 16:9 opcional | 16:9 com correção do HUD (desenhos 2D por faixa de profundidade e posição) e da projeção só onde viável; sem dados confiáveis, barras laterais |
| Filtragem | original (a do jogo por desenho), bilinear, anisotrópica | o "original" é a amostragem manual atual; bilinear e anisotrópica pelo amostrador do hardware, com mipmaps gerados |
| Antialiasing | MSAA (2×, 4×, 8×, conforme a GPU) | o autosort por pixel passa a guardar a máscara de amostras; antes disso, MSAA só com translúcidas na ordem |
| Pós-processamento | nitidez, CRT, ajustes de cor | passes depois do quadro, desligados no "Original" |
| Presets | Original Dreamcast, Equilibrado, Alta Qualidade, Personalizado | o "Original" é exatamente a configuração de equivalência (1×, 4:3, amostragem original, sem efeitos) |
| Capacidades | detecção (MSAA máximo, anisotropia, formatos, memória) | opção sem suporte: desligada com aviso; falha do Vulkan: recuo para o software |
| Persistência | arquivo externo (`graficos.toml` ao lado de `controles.toml`) | lido na inicialização, gravado pelo menu (seção 17) |

## 16. Shaders originais e melhorias opcionais de iluminação (complemento do mantenedor, 2026-10-21)

**1. Fidelidade primeiro (parte do N7b, antes de qualquer melhoria):**
- os estados do PVR2 em shader (já feitos: seção 14);
- **fog** por tabela (128 entradas de 16 bits, `FOG_DENSITY`) e por vértice, com `FOG_COL_RAM` e
  `FOG_COL_VERT`. Os dados já estão em `FrameParams`, mas o software não aplica fog; a comparação é
  com o Flycast;
- transparências, mistura e iluminação por vértice (as cores que o jogo calcula: já vêm na IR);
- **volumes modificadores e sombras:** stencil por volume (inclusão ou exclusão do último polígono),
  com a lista 1 sobre opacos e punch-through e a lista 3 sobre translúcidos. Os desenhos marcados
  usam o segundo conjunto de parâmetros (cor ou textura alternativa), que precisa entrar na IR (o
  TA hoje marca "dois volumes" como não suportado e o jogo não os usou na rota). Comparação com o
  Flycast nas cenas com sombra.

**2. Melhorias opcionais**, cada uma desligável sozinha e **todas desligadas no preset Original**:
- iluminação aprimorada só onde os dados do jogo permitem (as cores por vértice já vêm calculadas; não
  há normais na IR). Nada de luzes dinâmicas inventadas nem de materiais alterados sem dados
  confiáveis;
- SSAO (a partir do 1/w), bloom, correção de cor, gamma e tonemapping;
- sombras melhores: só se os volumes modificadores do jogo permitirem mais resolução ou suavização
  sem mudar onde a sombra cai;
- shaders configuráveis, preparados para mods (os mods de shader e as ferramentas ficam no N10).

**3. Ordem:**
1. equivalência do Vulkan na resolução original (N7b);
2. resolução HD/4K, filtragem e antialiasing (N7c);
3. iluminação e pós-processamento opcionais, conforme a viabilidade.

## 17. Menu interno de configurações gráficas (requisito obrigatório, mantenedor, 2026-10-21)

Todas as opções gráficas ficam acessíveis **dentro do jogo**, sem parâmetros de linha de comando nem
edição de arquivos. As opções de linha de comando continuam, para automação e desenvolvimento.

**Arquitetura (preparada agora; o menu funcional vem depois de o backend Vulkan estabilizar):**
- **Configurações** (`native/platform/settings`): modelo e persistência (TOML), separados da
  interface e do renderizador. É o mesmo sistema de configuração dos controles, com seções futuras
  para áudio, controles e acessibilidade.
- **Capacidades do renderizador:** o backend informa o que suporta; a interface só oferece o que
  existe.
- **Aplicação:** o renderizador recebe a configuração nova entre quadros. Uma mudança segura
  (filtragem, efeitos) vale na hora. Uma mudança de resolução ou de modo de tela recria a swapchain
  e os alvos, sem tocar no estado do jogo.
- **Interface:** sobreposição desenhada pelo próprio backend, com navegação por controle Xbox,
  teclado e mouse e visual coerente com o jogo. Abre pelo menu principal (Configurações → Gráficos)
  e pelo menu de pausa quando possível: uma tecla ou botão dedicado pausa o jogo **sem mexer na
  lógica dele**.

**Critérios de aceitação:**
1. Configurações → Gráficos aparece no menu principal e, quando possível, na pausa.
2. Estão no menu: resolução, escala, proporção 4:3/16:9, tela cheia, V-Sync, antialiasing,
   filtragem, texturas (original ou HD, quando houver), iluminação, sombras, shaders e
   pós-processamento.
3. Presets Original Dreamcast, Equilibrado, Alta Qualidade e Personalizado. Mudar uma opção
   isolada leva ao Personalizado.
4. Mudanças aplicadas durante o jogo, quando seguro, sem perder progresso (a partida continua do
   mesmo ponto).
5. As preferências são salvas automaticamente e carregadas na inicialização seguinte.
6. "Restaurar padrões". Uma mudança de resolução ou modo de tela pede confirmação e volta sozinha
   à configuração anterior em 15 s sem resposta.
7. Navegação completa por controle Xbox, teclado e mouse.
8. O jogo é jogável e configurável sem nenhum parâmetro de inicialização.

**Testes previstos:**
- unitários do modelo (presets, persistência, valores inválidos, reversão);
- roteiro que abre o menu, muda e confirma com controle virtual (`BS_CONTROLE_VIRTUAL`);
- reversão automática sem confirmação;
- o estado do jogo por passo é igual com e sem o menu aberto.

## 18. N7c: legendas das falas durante o gameplay (requisito obrigatório)

O N6 já decide **que texto** está ativo e **quando**:
- `SubtitleService`, alimentado pela `DialogueBridge` com os eventos das falas nativas, no tempo
  lógico do mixer;
- pausa, interrupção e troca de cena encerram as falas.

O N5 desenha as legendas dentro do quadro do filme. O que faltava é **desenhar as falas sobre o
gameplay**, sem duplicar nada disso.

**Desenho (camada visual separada):**
- **`GameFrame::subtitle`:** o texto das falas ativas é publicado **com o quadro do mesmo VBlank**
  (`GameHost::dialogueVblank`). Legendas desligadas (`--legendas nenhuma`, `legendas.toml`)
  produzem texto vazio.
- **`app/subtitle_overlay`:** desenha com o rasterizador DirectWrite das legendas dos filmes
  (`native/video`, modo `draw_overlay`) numa **camada transparente** (ARGB pré-multiplicado):
  - texto embaixo e centralizado na área do jogo;
  - margem segura e quebra de linha;
  - tamanho proporcional à altura da área do jogo;
  - contorno e/ou fundo translúcido.

  A imagem do jogo **não é tocada**: o hash dos quadros (`BS_FRAME_LOG`), as comparações de
  fidelidade e o Vulkan × software não veem a legenda.
- **Na janela:** a camada é feita na **resolução de saída** (texto nítido em qualquer escala, sem o
  borrado de ampliar 640×480) e misturada por cima do quadro (`SDL_BLENDMODE_BLEND_PREMULTIPLIED`).
  É refeita só quando o texto ou o tamanho mudam. O mesmo vale para software e Vulkan, porque os
  dois entregam o mesmo quadro.
- **Capturas** (`--capture`, F12, `BS_CAPTURA_VBLANKS`/`BS_CAPTURA_CRUA` para os testes): a camada é
  composta sobre uma cópia do quadro, ou seja, o que o jogador vê.
- **4:3, 16:9, redimensionamento e tela cheia:** a camada recebe a **área do jogo** (`cx, cy, cw,
  ch`). Hoje é a saída inteira; com barras ou 16:9 (N7c), é o retângulo da imagem, e o texto fica
  dentro dele. Uma mudança de tamanho refaz a camada.
- **Mods (N10):** o texto vem dos pacotes (`legendas/falas.<idioma>.toml`), com o idioma por
  `legendas.toml` ou pela linha de comando, e a fonte pode vir do pacote (`SubtitleStyle::font`, já
  previsto).

**Critério de aceitação:** uma fala identificada pelo N6 aparece **na imagem apresentada**, no
momento certo, em gameplay real; não basta o evento no log. O teste é
`tools/dev/n7_legendas.py --sessao sessao-20261021-145006 [--render software|nativo]`:
- reproduz a gravação real;
- captura as faixas das falas;
- exige diferença entre o quadro composto e o cru **na faixa de baixo** enquanto a fala está ativa;
- exige **nenhuma legenda** antes, entre e depois das falas (sem legenda persistente).

**Resultado (2026-10-22):**
- **Software:** APROVADO, com 866 quadros com fala, todos com a legenda na faixa de baixo, e 338
  quadros sem fala, limpos.
- **Vulkan:** APROVADO, com 860 e 341.

As 17 linhas cobertas são das vozes `e025a`, `e025b` e `e005b`, sobre música e com troca de cena.

**Unitários:** `subtitle_overlay_*`:
- texto só embaixo;
- quadro sem legenda idêntico;
- tamanho e fundo;
- mistura pré-multiplicada.

## 19. N7c: composição, menu interno e resoluções modernas (arquitetura)

### Camadas da apresentação

| Ordem | Camada | Resolução | Efeitos |
|---|---|---|---|
| 1 | Imagem do jogo (software ou Vulkan; o HUD original **dentro** dela) | interna (fiel: 640×480; N7c: 720p a 4K) | os do PVR2 |
| 2 | Pós-processamento opcional (nitidez, CRT, cor, bloom, SSAO) | interna ou de saída | só na camada 1 |
| 3 | Legendas e interface nativa (menu) | **de saída** (nítida) | **nenhum**: o CRT e o bloom não tocam texto nem menu |
| 4 | Apresentação na janela e capturas | de saída | — |

Hoje as camadas 1, 3 e 4 existem: a imagem do jogo vai pelo framebuffer emulado até a janela SDL, e
a camada 3 vai por cima.

As resoluções maiores exigem que o Vulkan **apresente direto** (swapchain própria), sem passar a
imagem pelo framebuffer de 640×480:
- a imagem do jogo continua a mesma IR;
- o modo fiel continua sendo a referência;
- o caminho aprimorado (sem `F_EXACT`, interpolação da GPU, escala) é separado e opcional.

### Menu interno (Configurações)

**Estrutura:**
- **Gráficos:** os campos de `graphics_settings.h` e os presets.
- **Áudio:** volumes geral, música, efeitos e vozes (`user_settings.h`, `audio.toml`), multiplicados
  no fim do mixer do N6, sem mexer no que o jogo manda.
- **Legendas:** ligar/desligar, idioma dos pacotes encontrados, tamanho, contorno e fundo
  (`legendas.toml`; inglês ligado por padrão).
- **Controles:** abre o mapeamento existente do N3 (`controles.toml`, `InputSystem`), sem
  reimplementar.

**Separação:**
- **Modelo** (`native/platform/*_settings`, portável e testado): valores, faixas, presets,
  persistência, `apply_kind` e `requires_confirmation`.
- **Capacidades** (`Capabilities` + `Features`): só o que o renderizador **já implementa** aparece
  como opção ativa; o resto aparece indisponível ("em breve"), nunca como enfeite.
- **Aplicação:**
  - imediata: filtragem e efeitos;
  - recriação entre quadros: tamanho, modo de tela, MSAA e V-Sync;
  - com confirmação e reversão em 15 s: tamanho e modo de tela.
- **Interface:** desenhada na camada 3, pelo mesmo rasterizador das legendas (texto) mais retângulos
  simples.

**Abertura e pausa:** uma tecla ou botão dedicado (Esc / Back do controle, configurável) abre o
menu e **segura o jogo entre VBlanks**, do mesmo jeito que o gameplay assistido já pausa
(`app/assist.h`), sem tocar a lógica do Dreamcast. Enquanto o menu está aberto, a entrada vai para o
menu e não para o jogo.

Também pelo menu principal do jogo: uma opção "Configurações" sobre a tela de título exige
reconhecer a tela (o ID do estado do título), a definir com o gameplay assistido. A tecla dedicada
funciona em qualquer tela.

**Ordem de implementação:**
1. modelos e testes (feito: `graphics_settings`, `user_settings`);
2. a sobreposição do menu com navegação (controle, teclado, mouse) e a pausa entre VBlanks (**feito**,
   2026-10-22: `app/menu_overlay`, `InGameMenu` em `app/main_sdl.cpp`, `GameHost::setPaused`);
3. as seções Legendas e Áudio (**feito**: tamanho, contorno, fundo, idioma, volumes no mixer do N6);
4. a seção Gráficos (**feito** o que já existe: renderizador software/Vulkan ao vivo, modo e tamanho da janela com
   confirmação, V-Sync; o resto acende conforme as `Features` do N7c ficam prontas);
5. Controles (mostra o perfil e o `controles.toml` do N3; remapear pelo menu fica "em breve").

**Resultado (2026-10-22):** validado em janela com o jogo real e controle virtual (ver `STATUS.md`, "N7c: menu
interno"). Decisões tomadas na implementação:
- os pedidos do menu (volumes, idioma, renderizador) são aplicados **na thread do jogo**, no início do VBlank ou
  durante a pausa, nunca da thread da janela;
- a área do jogo é calculada a cada quadro em pixels de saída (`gameArea`), sem a apresentação lógica do SDL, para o
  quadro, as legendas e o menu ficarem no mesmo espaço depois de trocar o modo de tela;
- a tela só vai para o arquivo depois de aceita; opções da linha de comando não vão para o arquivo.
- A opção "Configurações" desenhada no menu do título não foi feita (exige reconhecer a tela); a tecla dedicada
  funciona em qualquer tela.

### Resoluções modernas (preparação)

- **Resolução interna** (`internal_height`): 480 (fiel), 720, 1080, 1440, 2160 × escala de 50 a
  200 %. O alvo do Vulkan e a IR são escalados: posições × fator, recortes e tesoura × fator, 1/w
  inalterado.
- **4:3 e 16:9.**
  - A projeção do jogo é a do Dreamcast, então ampliar a 16:9 exige conhecer as matrizes (análise do
    gameplay).
  - Sem isso, 16:9 é 4:3 com barras e as legendas dentro da área do jogo.
  - O HUD é reposicionado só com dados confiáveis (`Features::aspect_wide`).
- **60 FPS:** não. O ritmo é o do relógio lógico (N2).

Critérios a cumprir antes de qualquer opção acender no menu:
- a imagem no modo fiel continua idêntica ao software, na resolução original;
- cada modo aprimorado tem um teste de imagem (PSNR ≥ 40 dB contra o fiel ampliado, medido).

## 20. N7c: resoluções modernas (A → B)

**Decisão do mantenedor (2026-10-22):** A primeiro, mínimo, para validar a renderização interna em 1080p com a
infraestrutura existente; depois B, o caminho definitivo: apresentação direta pelo Vulkan.

### Resolução interna no modo fiel (comum a A e B)

- Escala `e = altura interna / 480`; o alvo do Vulkan é `round(640 e) × round(480 e)`.
- O shader do modo fiel avalia as contas do software (cobertura com a regra de cima/esquerda, 1/w, atributos,
  divisão exata) no ponto `(pixel do alvo + 0,5) / e` das coordenadas do jogo.
- Na escala 1 o ponto é o mesmo de antes (×1,0 é exato): a igualdade bit a bit com o software se mantém.
- O framebuffer emulado recebe a imagem reduzida pela média (`frame_prep: downsample`). O jogo, as capturas e o
  hash dos quadros continuam coerentes.
- A apresentação acha qual render está sendo exibido pelo conteúdo (os 3 últimos, tolerando a quantização do
  framebuffer) e usa a imagem maior dele nas linhas exibidas (entrelaçado: 476 de 480).
- Texturas: as originais, amostradas em mais pontos (bilinear no shader). HD continua no N9 (remasterização).

### Caminho A (feito, 2026-10-22)

A imagem maior volta inteira para a CPU e a janela SDL a mostra. Validado em 1080p: ver `STATUS.md`, "N7c:
resolução interna, caminho A". Não é a arquitetura final: o custo da transferência (6,2 MB e 3,5 ms por render em
1080p) cresce com a resolução.

### Caminho B (definitivo)

- Um dispositivo Vulkan por processo, compartilhado pelo renderizador (thread do jogo) e pela apresentação (thread
  da janela), com a fila sob trava.
- Depois de cada render na escala:
  - cópia na GPU para um anel de slots apresentáveis;
  - redução na GPU (compute, as mesmas contas inteiras da CPU);
  - só a imagem na resolução do jogo volta para a CPU (1,2 MB, o mesmo tamanho que o modo fiel já lê).
- Apresentação: swapchain própria na janela. A imagem do jogo (o slot, sem passar pela CPU, ou a imagem da CPU:
  software, filmes) vai na área 4:3, e menu e legendas são camadas na resolução da janela (texto nítido).
- O renderizador em software e a apresentação SDL continuam como recuo.
- Resolução interna, resolução da janela e escala de renderização ficam separadas.
- 1440p e 4K só depois da apresentação direta estabilizar, respeitando os limites da GPU.
- 16:9 só com a projeção, a câmera e o HUD corrigidos (até lá, 4:3 com faixas).

**Caminho B: feito (2026-10-22).** Validado com gameplay real, de 1080p a 4K, em janela 4:3, 16:9 e sem bordas
21:9 (`STATUS.md`, "N7c: apresentação direta pelo Vulkan, caminho B"). Decisões tomadas na implementação:
- duas filas da mesma família (renderizador e apresentação): o `vkQueuePresentKHR` com V-Sync pode esperar o monitor
  e, numa fila só, segurava a thread do jogo;
- slots em `GENERAL`, semáforo por escrita, fixação até a cerca da apresentação;
- 1440p e 4K liberados depois da apresentação direta estabilizar, até o limite da GPU (`max_internal_height`).

### Critérios de aceitação (resoluções)

1. O modo original continua idêntico pixel a pixel à referência do software (rota de combate, 9000 quadros).
2. Na resolução maior, a geometria é desenhada no alvo maior. A prova é o teste de borda do triângulo e o detalhe
   acima da resolução original maior que o da imagem original ampliada (`n7_resolucao.py`).
3. Medidos: qualidade, tempo de GPU, tempo de apresentação, custo das transferências (tabela do `STATUS.md`).
4. Menu, legendas e HUD conferidos em janela 4:3, 16:9 e 21:9 (`n7_janela.py`).
5. A pendência AMD continua aberta até a evidência.

## 21. N7c: pós-processamento e shaders personalizados (complemento do mantenedor, 2026-10-22)

**Objetivo:** iluminação visualmente melhor e shaders personalizados, sem alterar os recursos originais e sem perder o
visual original.

### Cadeia

- Na apresentação direta, **entre a imagem do jogo e a composição** das camadas nativas: os efeitos tocam só a
  imagem do jogo. Menu e legendas vêm depois, nítidos.
- Cada passe é um shader de fragmento de tela cheia. Lê a imagem anterior (e, quando o passe declarar, a original) e
  escreve numa imagem intermediária na resolução da fonte (a interna, ou 640×480 para o software).
- O último passe vai para a área 4:3 da janela.
- **Original Dreamcast:** a cadeia é vazia, e o caminho é exatamente o de hoje (nenhum passe extra, a mesma imagem).
- Ordem fixa dos internos:
  1. correção de cor (brilho, contraste, gamma, saturação);
  2. tonemapping;
  3. bloom (limiar, desfoque em pirâmide, soma);
  4. nitidez (adaptativa).
- Os externos entram na posição que o manifesto declara (antes ou depois dos internos).
- Persistência em `graficos.toml` (`[efeitos]` e `[shaders]`). Os presets Equilibrado e Alta Qualidade
  ("aprimorados") ligam efeitos; Personalizado guarda qualquer combinação.
- Menu: cada efeito liga e desliga sozinho, com os parâmetros. A mudança vale no quadro seguinte (sem confirmação:
  não muda o tamanho da tela).

### Shaders externos (formato; pacotes completos e ferramentas no N10)

- **Pasta:** `mods/shaders/<nome>/`, com `manifesto.toml` e `shader.spv`.
- **Formato:** SPIR-V já compilado. O jogo não compila GLSL; quem cria usa o `glslangValidator`. O fonte GLSL vai
  junto, por transparência.
- **O que o manifesto declara:**
  - nome, versão do formato (`formato = 1`) e descrição;
  - a posição na cadeia;
  - até 16 parâmetros float: nome, rótulo, mínimo, máximo, padrão e passo.
- **Interface fixa do shader:**
  - só estágio de fragmento, ponto de entrada `main`;
  - `set 0, binding 0`: `sampler2D` da imagem anterior;
  - `binding 1` (opcional): a original;
  - bloco de push constants com o tamanho da imagem e os 16 parâmetros (até 80 bytes);
  - saída `location 0`.
- **Validação antes de usar:**
  - número mágico e versão do SPIR-V;
  - a reflexão mínima do próprio jogo: modelo de execução Fragment, `main`, só os bindings permitidos, push
    constants dentro do limite, nenhuma outra entrada ou saída;
  - `vkCreateShaderModule` e o pipeline criados sem erro.
- **Falha em qualquer passo:** o shader fica desativado, com o motivo no log e no menu ("inválido: ..."). O jogo
  continua com os outros efeitos.
- **Uma perda do dispositivo durante um passo externo** o desativa para a sessão, e a recuperação do N7b recria o
  dispositivo.
- **Segurança:** nada de código nativo de mods. O shader só lê as imagens que a cadeia entrega e só escreve na saída
  do passe; não há buffers graváveis nem acesso à memória do jogo.

### Iluminação, SSAO e sombras (preparados, sem inventar dados)

- **SSAO:** só com a profundidade que o renderizador já tem (o 1/w das opacas, no alvo da resolução interna).
- **Luz e sombras:**
  - interface reservada (`PostInput`: profundidade e, no futuro, normais ou dados de luz confiáveis);
  - nada de luz inventada, e a lógica do jogo não muda;
  - os efeitos de iluminação desta etapa são de imagem (bloom, tonemapping, cor), que não fingem dados que o jogo não
    fornece.

### Estado (2026-10-22)

- **Feito:**
  - a cadeia;
  - a correção de cor interna;
  - o validador e o manifesto;
  - o exemplo externo (`mods-exemplos/shaders/cor-quente`);
  - a página "Efeitos de imagem" do menu, a persistência e a desativação com diagnóstico (`STATUS.md`, "N7c:
    pós-processamento e shaders de mod").
- **Formato dos mods:** `docs/MODS-SHADERS.md`.
- **Decisões tomadas na implementação:**
  - **laços recusados no SPIR-V:** um passe tem de terminar. Laços com orçamento de execução, junto com vários
    passes e LUTs, no N10;
  - uma perda do dispositivo com um shader de mod ativo o desativa;
  - os passes rodam na resolução da fonte (a interna, se houver), e o último vai para a área 4:3.

### Ordem e critérios de aceitação

1. Cadeia mínima (correção de cor interna) e o **shader externo de demonstração** (correção de cor) pelo menu:
   **feito**.
2. Testes (**aprovados**):
   - liga e desliga em tempo real (a imagem muda; desligado, volta igual pixel a pixel à de antes);
   - persistência depois de reiniciar;
   - shader inválido (SPIR-V corrompido, binding fora da interface, push constants grandes demais) desativado com o
     motivo, sem derrubar a imagem;
   - o preset Original continua sem nenhum passe.
3. Depois, um efeito por vez: tonemapping, nitidez, bloom; SSAO por último, se a profundidade servir.
4. Pacotes completos de mods gráficos e ferramentas de criação: N10.

## 22. N7c: o que falta e o roadmap N0–N10 (decisão do mantenedor, 2026-10-22)

**Roadmap:**
- **N9** passou a ser a **remasterização**: recursos audiovisuais oficiais do projeto, com o preset Remasterizado.
- O antigo escopo de mods foi para o **N10**: ferramentas, manifestos, carregador de pacotes, idiomas, texturas,
  áudio, vídeos, shaders e a elevação dos sistemas do jogo.
- Neste documento, "N9" nas texturas HD do produto e no widescreen quer dizer a remasterização; o resto, N10.
- A arquitetura de extensão continua preparada aqui (contrato das texturas HD, shaders de mod validados) sem antecipar
  a implementação completa do N9 nem do N10.

**Pendências do N7c (antes de fechar o N7):**
1. Efeitos internos restantes, um por vez: tonemapping, nitidez, bloom; SSAO só se a profundidade servir.
2. **Segundo analógico e câmera livre** (pedido do mantenedor). O controle do Dreamcast tem um analógico só, e a
   câmera do Blue Stinger é do jogo: não há entrada que o jogo leia para girá-la. A ordem do trabalho:
   - investigar, com o gameplay assistido, onde o jogo guarda e atualiza a câmera (estrutura da câmera, modos fixo e
     de perseguição, quem escreve a cada passo de jogo);
   - propor como mapear o analógico direito, sem mudar a lógica no modo original:
     - opção desligada por padrão;
     - o deslocamento aplicado à câmera depois do cálculo do jogo e só para a imagem, sem tocar a colisão, a IA nem
       o que o jogo lê de volta;
   - decidir com o mantenedor antes de implementar, porque toca o comportamento do jogo;
   - o mapeamento do controle continua o do N3 (`controles.toml`).
3. Validação com as camadas do Vulkan num terminal não elevado (pendência AMD e caminho B).
4. Fechamento do N7 (nível 2): testes completos do subsistema e comparação com o legado.
