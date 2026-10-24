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
3. Depois, um efeito por vez: tonemapping, nitidez, bloom (**feitos**, mais o CRT); SSAO por último, se a profundidade
   servir. **Pendente:** a projeção ainda não foi localizada (seção 23), e o HUD 2D teria de ser separado da cena.
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

## 23. Câmera livre e segundo analógico: investigação, proposta (aprovada em 2026-10-22) e implementação (23.2)

### Método

Nada do jogo foi alterado para investigar.
- **Estático:**
  - `bs-disasm` sobre o `1ST_READ.BIN`;
  - as constantes dos literal pools na região da câmera e as instruções que as carregam (o código gerado lê cada
    literal da memória: `MOVL_PC`).
- **Dinâmico, no build de investigação** (`-DBS_WRITE_WATCH=ON`, `build/n7-ww`):
  - o registro novo `BS_ESCRITOR=<lo>-<hi>:<arquivo>` grava quem escreve numa faixa da RAM: a função em execução
    (`c->cur_fn`), o PR (o chamador) e o valor, agregado por endereço;
  - no executável normal nada disso existe (`#if BS_WRITE_WATCH`).
- `BS_RAM_WATCH` (já existente) nas variáveis da câmera, ao longo da rota `passo_combate` (9000 VBlanks).

### O que foi encontrado

**Objeto câmera do SDK (Ninja), `0x8C24CC38`, ~0x78 bytes:**

| Endereço | Conteúdo | Quem escreve (função; chamador) |
|---|---|---|
| `CC38–CC77` | matriz de visão 4×4 (float) | `8C1741EC` (monta a matriz a partir da câmera), com `8C170D34`, `8C170DC8`, `8C170E64` e `8C1713A4` |
| `CC78–CC80` | posição da câmera (x, y, z) | `8C174308` (posição = vetor base + deslocamento em fr4–fr6) |
| `CC84–CC8C` | vetor de visão | `8C1740DC` (inicia), `8C174148` (gira em X: y, z) e `8C174180` (gira em Y: x, z), com o ângulo em r5 (unidade Ninja: 0x10000 = 360°, `fsca`) |

**Câmera do jogo (estado lógico), `0x8C24CD50`:**

| Endereço | Conteúdo (medido na rota) |
|---|---|
| `CD50–CD58` | posição da câmera no mundo (segue o Eliot, a ~280 unidades no gameplay) |
| `CD5C` | pitch (inteiro Ninja; somado a um valor da tabela de câmeras `0x8C137A74`, 104 bytes por entrada, índices `CD6A`/`CD6C`) |
| `CD60` | yaw (inteiro Ninja) |
| `CD68` | 0/1; vira 1 na cena de abertura (VBlank 2279) e nos momentos roteirizados (4656, 6968): muito provavelmente a câmera de evento (a confirmar) |

`CD50` é lido por 45 funções do jogo: é estado de lógica, não só de imagem. O Eliot fica em `0x8C24D668` (x, y, z).

**Montagem da câmera por passo de jogo, em `8C052240`** (a atualização principal do jogo; base `r14 = 0x8C24D2CC`):
1. `8C05285C` `8C1740DC(cam)`: reinicia a câmera do SDK.
2. `8C05288E` `8C174148(cam, pitch)`: pitch = `[CD5C]` + o da tabela.
3. `8C052896` `8C174180(cam, [CD60])`: yaw.
4. `8C0528A8` `8C174308(cam, [CD50], [CD54], [CD58])`: posição.
5. `8C0528B0` `8C1741EC(cam)`: matriz de visão em `CC38`.
6. Depois, um segundo objeto em `0x8C2D0CC0` recebe a posição NEGADA (`8C0528D6`; céu/fundo ou luz, a confirmar).

Um segundo caminho, `8C078FC0` (`8C07964C`–`8C07966E`), faz a mesma montagem sem o giro em Y: provavelmente a câmera
fixa ou de evento. Na rota:

| Caminho | Passos |
|---|---|
| `8C052240` (gameplay) | 2634 |
| `8C078FC0` | 672 |

As chamadas ao SDK são **indiretas** (`jsr @r3`): passam pelo despachante, e `sh4_override` pode interceptá-las sem
mexer no código gerado. O PR identifica o ponto de chamada.

**Matrizes para efeitos (SSAO, iluminação):**
- a matriz de visão por quadro está em `CC38` (escrita em `8C0528B0`);
- a projeção não está nesse objeto (o PVR recebe vértices já projetados); falta achar os parâmetros de tela do SDK
  (distância e centro). Com eles e o 1/w, a GPU reconstrói a posição no espaço da câmera.
- O SSAO continua pendente até isso e até separar o HUD (2D) da cena.

### Proposta (aguarda a decisão)

**Ponto de integração: só a montagem da câmera para a imagem, nunca o estado do jogo.**
- Substituições (`sh4_override`) de `8C174148`, `8C174180` e `8C174308`. Elas aplicam o deslocamento do jogador **só
  quando o PR é o da montagem do gameplay** (`8C052892`, `8C05289A`, `8C0528AC`); qualquer outro chamador recebe a
  função original sem mudança.
- Com os deslocamentos `Δyaw` e `Δpitch` (do analógico direito, no relógio lógico):
  - yaw' = yaw + Δyaw e pitch' = pitch + Δpitch (limitado);
  - posição' = alvo + R(Δyaw, Δpitch) · (posição − alvo), uma órbita em volta do Eliot (`D668` mais a altura dos
    olhos), com a distância do jogo e limites;
  - o segundo objeto (`0x8C2D0CC0`, posição negada) recebe a mesma posição' (o mesmo PR, `8C0528DA`).
- **Movimento:**
  - o analógico esquerdo continua indo para o jogo;
  - nos modos modernos, o vetor dele é girado por −Δyaw antes de o jogo o ler (`pad_adapter`). "Para cima" segue a
    câmera que o jogador vê; o jogo continua movendo o Eliot relativo à câmera dele.
- **Gravações e roteiros:** a gravação guarda o que o JOGO leu (depois do giro) e reproduz igual em qualquer modo de
  câmera.
- **Modos:**
  - **Original Dreamcast:** nenhuma substituição instalada; o caminho é exatamente o de hoje (prova: rota 9000/9000
    quadros idênticos);
  - **Híbrida:** o analógico direito gira a câmera e ela volta sozinha à do jogo depois de N segundos sem
    usá-lo (retorno automático);
  - **Livre:** o deslocamento fica até o R3 recentralizar.
  - Nas câmeras de evento (`CD68` = 1, a confirmar) e no caminho `8C078FC0` (fixa ou de evento), o deslocamento vai a 0
    suavemente: cutscenes, diálogos e eventos roteirizados ficam com o enquadramento original.
- **Opções** (menu Controles > Câmera, `controles.toml`):
  - modo;
  - sensibilidade horizontal e vertical;
  - zona morta;
  - inverter Y;
  - suavização;
  - limites de pitch;
  - retorno automático (segundos);
  - R3 recentraliza.
  - A taxa é por passo de jogo (independente da taxa de quadros da janela).

**Riscos e como validar:**
- **Recorte (culling) pelo jogo:** o jogo pode descartar objetos pela posição ORIGINAL da câmera (`CD50`). Com Δ grande
  podem sumir objetos nas bordas. Validar girando 360° em salas típicas; se acontecer, limitar o Δyaw por sala ou
  usar o modo híbrido com retorno.
- **Atravessar paredes:** o jogo não testa colisão da câmera orbitada. Mitigar com distância e pitch limitados;
  tratamento de obstáculos é um passo posterior (raio contra a geometria de colisão, ainda não localizada).
- **Leituras de `CC84–CC8C`** (vetor de visão) por outros sistemas (som 3D, mira): só a imagem muda, e o estado do
  jogo continua o original.
- **Confirmar antes de implementar:**
  - quem lê `CD5C`/`CD60` e o vetor de visão para o movimento (registro de leituras no build de investigação,
    como o de escritas);
  - o significado de `CD68`.
- **Critérios de aceitação:**
  - Original idêntico (rota de combate, quadros e estado por passo iguais);
  - gravações reproduzem iguais em qualquer modo;
  - cutscenes e eventos com o enquadramento original;
  - 360° no cais, no corredor e no heliporto sem quebrar o movimento relativo;
  - nenhum custo mensurável com a câmera Original.

### 23.1 Verificações exigidas na aprovação (2026-10-22)

A proposta foi aprovada (interceptação filtrada pelo ponto de chamada, sem escrever em `0x8C24CD50`). Antes de
instalar qualquer substituição, foram feitas as verificações pedidas. O registro de leituras é novo, `BS_LEITOR`,
o par do `BS_ESCRITOR`, só no build de investigação. Rota `passo_combate`, 9000 VBlanks.

**Quem lê o quê:**

| Variável | Leitores (função; chamador; leituras) | Conclusão |
|---|---|---|
| vetor de visão `CC84–CC8C` | só o SDK da câmera: `8C1741EC`, `8C174148`, `8C174180` | nenhum sistema do jogo o usa |
| matriz `CC38–CC77` | só o SDK: a montagem (`8C1713A4`, `8C170D34`, `8C170DC8`, `8C170E64`) e `8C17083C` (211 mil, transformação de desenho) | nenhum código do jogo lê a matriz: mudar a montagem muda só a imagem |
| pitch `CD5C` / yaw `CD60` | `8C059BE0` (lógica da câmera, 3408); `8C052240` (montagem); `8C08AC60` (desenho de uma lista de sprites orientados pela câmera, 2632); `8C05D1C0` (ângulo de um objeto relativo à câmera, 1440) | estado do jogo: não é escrito (aprovação) |
| posição `CD50` | `8C05D15C` (distância objeto–câmera, 176 mil: o chamador `8C058050` trata objetos a menos de 100 unidades da câmera ORIGINAL); `8C08AC60` (os sprites, 50 mil) | com a câmera orbitada, esses objetos e os sprites seguem a câmera do jogo (risco visual, abaixo) |

**`0x8C24CD68`:**
- escrito com 1 por `8C062BFE` (chamado de `8C035AEE`, `8C036F56`, `8C03711A`) e com 0 por `8C058720`, `8C05E7E0`
  e `8C062DDC`;
- lido pela lógica da câmera, pela montagem e por `8C058720`;
- nas capturas, coincide com as cenas roteirizadas nos três intervalos da rota:

| VBlanks | CD68 | Na tela |
|---|---|---|
| 2279–4491 | 1 | cinemática de abertura (sem HUD) |
| 4656–5346 | 1 | cinemática da Kimra/holograma (sem HUD) |
| 5346–6968 | 0 | gameplay com a barra do ELIOT |
| 6968–8708 | 1 | "Press the START button" e o diálogo por rádio |
| 8708 em diante | 0 | gameplay com HUD |

- Conclusão: `CD68` = 1 nas câmeras roteirizadas (cinemáticas, eventos, diálogos com câmera própria).
- Limitação: comprovado na rota de combate. O tratamento é conservador: com CD68 = 1, ou fora do caminho de gameplay,
  o deslocamento volta a zero.

**Pontos de chamada da câmera de gameplay**, todos dentro de `8C052240`, com PR exato e chamadas indiretas
(`sh4_override` as intercepta). As funções do SDK são compartilhadas (`8C1713A4` tem 52 pontos de chamada, `8C170DC8`
33): o filtro pelo PR é obrigatório e qualquer outro chamador recebe a função original.

| Matriz | Chamada | PR | O que muda |
|---|---|---|---|
| objeto câmera (`CC38`) | `8C174148` pitch | `8C052892` | + Δpitch |
| | `8C174180` yaw | `8C05289A` | + Δyaw |
| | `8C174308` posição | `8C0528AC` | posição orbitada |
| transformação de desenho (`0x8C2D0CC0`) | `8C1713A4` translação por −posição | `8C0528DA` | −posição orbitada |
| | `8C170DC8` giro em Y por `~yaw` | `8C0528E4` | `~(yaw + Δyaw)` |
| | `8C170D34` giro em X por pitch | `8C05290C` | + Δpitch |

O caminho `8C078FC0` (câmera fixa/de evento) não é interceptado.

**Movimento:**
- nenhum leitor do pitch e do yaw é código de controle do personagem;
- o movimento continua relativo à câmera do jogo, que não muda: com a câmera livre girada, "para cima" segue a câmera
  original, não a vista;
- o analógico esquerdo não é girado (exigência da aprovação). Uma opção de movimento relativo à câmera vista fica para
  depois das validações pedidas.

**Riscos visuais confirmados:**
- objetos a menos de 100 unidades da câmera original, e os sprites orientados por ela (`8C08AC60`), seguem a câmera
  do jogo;
- com deslocamento grande, um objeto perto da vista nova pode tapar a imagem, e sprites podem aparecer de lado.

Mitigação: limites de yaw e pitch e o retorno do modo híbrido.

### 23.2 Implementação e validação (2026-10-22)

**Correção do mecanismo.** A seção 23 dizia que as chamadas da montagem eram indiretas e que `sh4_override` as
interceptaria. O código gerado mostra outra coisa: o recompilador resolveu os alvos (`BS_EXPECT_TARGET` +
`BSG_FN(8C174148)(c)`), então as chamadas são diretas e `sh4_override` não as vê. A interceptação usa o mecanismo
das fronteiras migráveis:
- as seis funções do SDK entraram em `[native] slots` (`config/bluestinger_us.toml`);
- regenerado, o código só troca a macro dessas chamadas (`BSG_FN` → `BSG_SLOT`), conferido arquivo a arquivo contra
  o gerado anterior;
- `transitional/free_camera` troca os slots pelo registro de fronteiras e filtra pelo PR, como aprovado. Qualquer
  outro chamador recebe a função anterior (o gerado, ou um mod) sem mudança.

**Semântica confirmada no disassembly:**

| Função | Argumentos e efeito |
|---|---|
| `8C1740DC` | zera o objeto; vetor de visão (0, 0, −1) |
| `8C174148` | gira o vetor de visão em X por r5 (y' = y·c − z·s; z' = z·c + y·s) |
| `8C174180` | gira o vetor de visão em Y por r5 (x' = x·c + z·s; z' = z·c − x·s) |
| `8C174308` | soma (fr4, fr5, fr6) à posição do objeto (+0x40) |
| `8C1713A4` (PR `8C0528DA`) | translação por (x, −y, −z) da posição do jogo (fr4 sem negação, fr5 e fr6 negados) |
| `8C170DC8` / `8C170D34` | giros em Y por `~yaw` e em X pelo mesmo pitch composto (`[CD5C]` + tabela) |

Ângulos em unidades Ninja (0x10000 = 360°, `fsca` usa os 16 bits baixos). O `CD68` é escrito com 16 bits
(`BS_ESCRITOR`). O segundo objeto (`0x8C2D0CC0`) só é lido pelo SDK (`BS_LEITOR`, rota de combate: `8C171330`, a
cópia para o desenho, chamada de `8C0841CA`, e as próprias primitivas).

**Geometria:**
- o deslocamento gira a vista em volta do ponto da linha de visão mais perto do Eliot (`D668`), com o raio limitado a
  20–600 unidades (medido no gameplay: 376–402);
- com o Eliot atrás ou embaixo da câmera, o raio cai e a vista gira no lugar;
- pitch final limitado a ±80°, além dos limites do jogador.

Sinais conferidos por captura: o yaw do jogo cresce girando a vista para a esquerda, e o pitch positivo olha para
cima.

**Suspensão e cortes:**
- com `CD68` ≠ 0, ou sem montagem de gameplay há 30 VBlanks, o deslocamento é zero e o rig recomeça do centro;
- um salto entre montagens maior que 60 unidades ou 40° é corte de câmera e também zera;
- medido: no gameplay a câmera anda até 39 unidades e 4° entre montagens (uma a cada 2 VBlanks); os cortes reais
  foram de 145 a 406 unidades e de 90° a 116°.

**Entrada (N3):**
- eixos `CAMERA_X`/`CAMERA_Y` e o botão `CAMERA_CENTRALIZAR` (padrão `rightx`, `righty`, `rightstick`), que o
  adaptador do controle não leva ao jogo;
- `InputSystem::peek` lê os dispositivos do host mesmo com roteiro (o jogo só vê o roteiro);
- no modo espectador o SDL Gamepad agora é iniciado (só para a câmera);
- a zona morta é a da câmera.

**Opções:**
- seção `[camera]` do `controles.toml`; o parser do N3 aceita a seção, e a gravação troca só ela (o resto do
  arquivo fica byte a byte, inclusive CRLF);
- menu Configurações > Controles > Câmera: modo, sensibilidades, zona morta, inverter Y, suavização, retorno
  automático (só Híbrida), R3, limites de pitch;
- `--camera original|hibrida|livre` vale só na execução;
- o menu aplica na thread do jogo, entre VBlanks: Original desinstala os slots.

**Diagnóstico:** `BS_CAMERA_ROTEIRO` (analógico por VBlank), `BS_CAMERA_LOG` (uma linha por montagem),
`BS_CAMERA_FIXA=yaw,pitch` e `BS_STATE_PASSO_SEM_CAMERA=1` (o hash de estado por passo sem o objeto câmera do SDK,
`CC38`–`CCAF`, que é o que a câmera livre muda).

**Validação (2026-10-22, build `n7-game` com o gerado novo; rotas com `--video legado`):**

| Teste | Resultado |
|---|---|
| Original com os slots novos, rota de combate | 9000/9000 quadros idênticos à referência; trabalho do guest idêntico (2 268 687 036) |
| Híbrida instalada, analógico parado | 9000/9000 quadros idênticos |
| Livre com yaw +30° e pitch +15° fixos (891 montagens com deslocamento) | estado por passo idêntico ao Original em 4321 passos; captura: a vista gira em volta do Eliot |
| Híbrida e Livre com o analógico por roteiro | Híbrida: gira 120°/s, segura 3 s, volta suave; pitch para no limite (25°); Livre: fica até o R3, que recentraliza suave; estado idêntico |
| Exploração (`passo_newgame_exploracao`, 16 000 VBlanks), Híbrida varrendo o analógico | 5191 montagens com deslocamento; estado idêntico em 7821 passos |
| Carregar save (`passo_load_slot1`), Livre fixa | estado idêntico em 2332 passos; pastas de saves iguais |
| Vulkan × software, mesmo deslocamento | 0 pixels diferentes (VBlank 6000) |
| Janela, espectador + controle virtual do SDL (`rightx`, `righty`, `rightstick`) | o analógico real gira, sobe até o limite e o R3 recentraliza |
| Janela, menu pelo controle virtual | Híbrida → Livre ao vivo; `[camera]` gravada, as 55 linhas anteriores iguais; o próximo início lê Livre; Original desinstala e grava |
| Unitários | 298/298 (rig, geometria, opções, seção, menu, entrada e os seis slots com sentinelas: PR estrangeiro, fora de ordem, `CD68`, RAM da câmera do jogo intacta, Original devolvendo os slots) |

**Custo:** com Original, nada instalado; o trabalho do guest é idêntico e o tempo por campo fica no ruído. Com a
câmera girada, o trabalho do guest muda 0,04%: o SDK desenha e recorta outra vista, como faria no console. O estado
do jogo continua idêntico.

**Limitações conhecidas:**
- objetos a menos de 100 unidades da câmera original e os sprites orientados por ela (`8C08AC60`) seguem a câmera
  do jogo;
- a câmera orbitada não testa colisão: com deslocamento grande, pode atravessar paredes;
- logo depois de cinemáticas, o jogo às vezes põe a câmera em cima do Eliot (VBlank 5346–5640 da rota de combate),
  e então a vista gira no lugar;
- `CD68` como "câmera roteirizada" foi comprovado na rota de combate; em outras cenas, o tratamento conservador é o
  mesmo;
- o movimento continuava relativo à câmera do jogo (analógico esquerdo intocado). Resolvido na seção 23.3.

### 23.3 Movimentação relativa à câmera (correção de usabilidade, 2026-10-22)

**Pedido do mantenedor.** Com a câmera girada pelo analógico direito, o analógico esquerdo para frente deve andar para
onde a vista olha, como num jogo moderno em terceira pessoa. O modo Original não muda. Os controles do N3 não são
reabertos.

**Investigação.**
- O direcional do jogo é relativo à câmera do jogo (N2b2-GAMEPLAY 1.4). A câmera livre só desloca a vista, nos
  argumentos das seis chamadas do SDK; ela não escreve em `0x8C24CD50`/`CD60`.
- Em gameplay, com a vista girada 90° e o analógico seguro, o Eliot mantém o rumo enquanto a câmera de perseguição do
  jogo gira para trás dele. O rumo não segue a câmera atual: segue uma **referência travada**.
- A referência é `0x8C24D680` (`0x8C24D2CC + 0x3B4`, na estrutura do Eliot), uma cópia de `CD60`. O jogo a grava em
  `8C041674`, `8C041830` e `8C04199E`. O `BS_RAM_WATCH` mostra a cópia feita no toque e refeita durante o toque quando o
  ângulo do analógico muda o bastante. É o que mantém a direção nos cortes de câmera.

**Implementação** (`transitional/free_camera`, `native/platform/input`, `camera_settings`, menu):
- **Entrada efetiva.** O analógico esquerdo é girado de **vista − `D680`**: o jogo soma a referência dele e o rumo
  resultante é a vista, sem rotação dupla. Só o yaw entra; o pitch não muda o movimento. O vetor é limitado ao círculo
  unitário antes de girar, então a diagonal do teclado não ganha velocidade. A zona morta é a do N3 (antes da rotação).
- **Vista ancorada no mundo.** Com o movimento relativo, o giro da câmera do jogo é descontado do deslocamento do rig
  (`CameraRig::absorb`). A vista fica onde o jogador a deixou, e o deslocamento cai sozinho a zero quando a câmera de
  perseguição chega atrás do Eliot. Nada disso acontece durante a volta da Híbrida, com o movimento "original" ou no
  Original.
- **Quando gira.** Só depois que a vista sai do enquadramento do jogo nesse toque. Sem deslocamento, a entrada passa
  intacta e o estado é idêntico ao Original. Em cena roteirizada (`CD68`), em corte, sem montagem há 30 VBlanks e com o
  analógico no centro, nada muda. Num corte com o toque já ativo, o salto da vista entra num desvio: o rumo continua,
  como no original. O D-pad não é girado.
- **Onde.** Na amostra do jogo (`InputSystem::latch`), **antes** do filtro da gravação: o `BS_GRAVAR` grava o analógico
  efetivo e a reprodução (fonte de teste) o entrega sem girar de novo. A reprodução não depende da câmera. Os
  diagnósticos da câmera (`BS_CAMERA_FIXA`, `BS_CAMERA_ROTEIRO`) giram também a fonte de teste.
- **Opção.** `[camera] movimento = "camera" | "original"` no `controles.toml`, com padrão `camera` (o Original da câmera
  não tem deslocamento). No menu: Controles > Câmera > Movimentação (Original / Relativa à câmera), que muda na hora.
  Para testes, `BS_CAMERA_MOVIMENTO`.

**Validação** (rota de exploração até o passo 3029 e depois o analógico exato, `AY=0`, por 300 passos; estado por passo
com `BS_STATE_PASSO_SEM_CAMERA`; rumo = deslocamento do Eliot em `0x8C24D668`):

| Caso | Resultado |
|---|---|
| Original + frente (R0) | a base |
| Livre com o movimento "original", vista 90° (R3, R9) | estado **idêntico** ao R0 em 3620 passos |
| Livre, vista 90° à direita pelo rig (R7) | rumo 92,7° com a vista em 92,8°; a câmera do jogo vai para trás do Eliot e o deslocamento cai a ~0 |
| Híbrida (R8) | igual ao R7; volta ao enquadramento do jogo depois de 3 s sem o analógico direito |
| Vista girando enquanto anda (R10) | o rumo acompanha a vista (−45,1°/−46,7° contra −44,3°/−46,8°) sem passar do ponto |
| Vista 180° pelo rig (R12) | rumo −177°, oposto à câmera do jogo (2,7°); um giro de 45° da câmera de perseguição numa montagem (posição +15) deixou de ser tratado como corte |
| Pitch de 30° (R4) | estado idêntico ao do mesmo caso sem pitch |
| Diagonal girada para a frente (unitário) | comprimento 1, sem ganho |
| Determinismo (R1 × R1b) | idêntico |
| Vulkan × software (R6) | estado idêntico |
| Combate inteiro (`passo_combate`, 9000 VBlanks), Híbrida sem tocar no analógico direito (R11) | **idêntico ao Original** em 4320 passos, 0 montagens com deslocamento: a mira e os disparos não mudam |
| Janela (`BS_GRAVAR` + controle virtual do SDL), vista girada, e a reprodução headless **sem** câmera livre | 231 amostras giradas gravadas como efetivas (ex.: `AY=145+AX=254`); estado **idêntico** em 3632 passos |

**Achados durante a validação (corrigidos):**
- a compensação agia também sem deslocamento e afastava a vista da câmera do jogo sem o jogador mexer no analógico
  direito (319 montagens no R11): agora só com deslocamento, e ele para em zero quando a câmera do jogo alcança a vista;
- a regra "yaw acima de 40° é corte" zerava o deslocamento num giro rápido da câmera de perseguição (R12): com o
  movimento relativo, o corte é só o salto de posição.

**Revisão do Codex** (`reviews/codex-20261023-003543.md`). O sandbox do Codex não lê o repositório nesta sessão
(`helper_sandbox_lock_failed`, `SetNamedSecurityInfoW` erro 5, no principal e na n7). O diff, o disassembly dos três
pontos de cópia e as medidas foram no prompt (`tools/dev/codex_review_inline.py`).
- **[MÉDIA, aceito]** O `absorb` normalizava antes de ver a passagem por zero: +170° com a câmera girando −20° virava
  zero em vez de −170°. Agora compara o valor sem normalizar, com testes dos dois sentidos.
- **[BAIXA, aceito]** O cálculo real (vista − `D680`) não tinha teste: virou `relative_degrees` (pura), testada com wrap
  `0xFFF0 → 0x0010`, os dois sentidos, ±180° e o desvio de corte.
- **Não verificado, registrado:** `BS_CAMERA_FIXA`/`ROTEIRO` junto com uma gravação reproduzida girariam de novo a
  entrada efetiva. São diagnósticos para roteiros com entrada crua (comentário no `GameHost`).
- **Conferido sem problema:** a leitura de 16 bits (o original mascara com `0xFFFF` nos três pontos), o wrap, os
  sinais, a ordem transformação → gravação, o Original intacto e a suspensão em cena e sem montagem.

**Limites.**
- Com o deslocamento fixo de diagnóstico (`BS_CAMERA_FIXA`), a vista fica presa à câmera do jogo e o Eliot a persegue.
  Não é um modo de jogo.
- A mira e o combate só foram validados sem deslocamento (idênticos ao Original). Com a vista girada, o Eliot vira
  para onde a vista manda, como no movimento.
- O D-pad continua relativo à câmera do jogo.

## 24. Fechamento do N7: critérios, situação e proposta (2026-10-22)

Preparado para a decisão do mantenedor; o N7 só fecha com ela. Nível 2 da política de testes: testes completos do
subsistema e comparação com o legado, sem a regressão integral.

### Situação por frente

| Frente | Situação | Evidência |
|---|---|---|
| N7a: captura do TA na IR | **concluída** | modo `conferir` em todo render da rota de combate: 0 divergências (seção 13) |
| N7b: backend Vulkan fiel | **concluída** | rota de combate, todo render comparado: NVIDIA 4487/4487 idênticos; AMD integrada 4486/4487 (1 recriação, ver AMD) (seção 14) |
| Validation Layers (validação completa + sincronização) | **concluída** | A1 AMD comparar, N1 NVIDIA nativo, J1 janela 1080p com a apresentação direta: 0 mensagens, depois de corrigido um hazard de sincronização da profundidade (`STATUS.md`, "Validação com as camadas do Vulkan") |
| Apresentação direta e resolução interna (caminho B) | **concluída** | original idêntico; 720p a 4K desenhados no alvo maior, limitados pela GPU (seção 20) |
| Menu interno (seção 17) | **concluído, com limites** | Gráficos, Áudio, Legendas, Controles, Restaurar padrões; pausa entre VBlanks; persistência; confirmação de tela com volta em 15 s. Limites: sem a opção "Configurações" desenhada no menu do título (a tecla vale em qualquer tela); remapear pelo menu "em breve" |
| Legendas no gameplay e nos filmes (seção 18) | **concluídas** | `n7_legendas.py` aprovado em software e Vulkan; pacotes `legendas-en` e `legendas-pt-br` como mods padrão locais (`mods-padrao/`, fora do Git), instalados pelo Builder; idioma automático pt-BR; filmes conferidos na imagem (`STATUS.md`) |
| Pós-processamento e shaders de mod (seção 21) | **concluídos** | cor, tonemapping, nitidez, bloom, CRT; shaders externos SPIR-V validados; o preset Original não aplica nenhum passe |
| Câmera livre (seção 23) | **concluída** | Original sem nada instalado (quadros idênticos); Híbrida e Livre com o estado do jogo por passo idêntico ao Original |
| Recuo sem Vulkan | **concluído** | sem dispositivo, o jogo segue no software com o motivo no log |
| GPU AMD integrada | **condicional** | perda intermitente do dispositivo no modo `comparar` (render 371, 1 em 3 rotas sem as camadas; nenhuma com as camadas). As camadas não acusam uso incorreto da API. A causa **não** foi identificada; a hipótese de driver não está comprovada. A recuperação recria o dispositivo. A apresentação direta não foi testada na AMD |
| Vulkan como padrão | **concluído (seção 25)** | decisão do mantenedor de 2026-10-23: Vulkan padrão na janela, recuo para o software; o `--headless` continua no software |
| Rotas além da de combate (Vulkan × software) | **parcial** | combate (todo render) e janelas com gameplay real; as rotas do `test_assistido` não foram comparadas render a render |
| Fog e volumes modificadores (seção 16) | **pendente de fidelidade** | o software de referência também não os aplica: o Vulkan é igual a ele, não ao console. A referência é o Flycast (oráculo gráfico, ainda não comparado) |
| MSAA, filtragem bilinear/anisotrópica, presets Equilibrado e Alta Qualidade | **não feitos** | o menu os mostra "em breve"; só o preset Original é real |
| 16:9 de verdade, texturas HD, iluminação e sombras aprimoradas | **não feitos** | contrato das texturas HD pronto (seção 13); 16:9 exige corrigir projeção e HUD |
| SSAO | **não feito**: ver abaixo | |

### SSAO: viabilidade

O que existe:
- a profundidade do renderizador: o 1/w do PVR das opacas e do punch-through, no alvo da resolução interna;
- a matriz de visão por quadro em `0x8C24CC38` (seção 23).

O que falta, e por que não cabe no N7 sem inventar dados:
1. **A projeção.** O PVR recebe vértices já projetados. A distância e o centro da tela do SDK (os parâmetros do
   `njSetScreen` ou equivalente) não foram localizados. Sem eles, a posição no espaço da câmera viria de um campo de
   visão suposto, que é exatamente o "dado inventado" que a seção 21 proíbe.
2. **A separação entre a cena e o HUD.** O HUD 2D vai nas mesmas listas, com o próprio 1/w. Sem uma marca por desenho
   (cena ou HUD) na IR, o SSAO escureceria as bordas do HUD e dos textos.
3. **A profundidade depois do render pass.** Hoje ela é descartada (`storeOp DONT_CARE`). Guardá-la e entregá-la ao
   pós-processamento é simples, mas só faz sentido com 1 e 2.

**Proposta:** transferir o SSAO para o **N9 (Remasterização)**, com os três pré-requisitos acima como primeira tarefa
dele. O menu continua mostrando "SSAO (em breve)" e o Original não muda.

### Proposta de fechamento

1. Encerrar o N7 com **promoção condicional**, como o N6, com as frentes concluídas acima.
2. Pendências abertas que ficam registradas:
   - a AMD integrada, sem causa identificada;
   - fog e volumes modificadores contra o Flycast;
   - rotas além da de combate.
   Fog e volumes devem ser resolvidos antes do N8, porque o N8 remove o legado, que inclui o rasterizador em software
   usado hoje como referência.
3. Para o N9: SSAO, 16:9, texturas HD, MSAA, filtragem, iluminação e sombras aprimoradas, e os presets que dependem
   delas.
4. Decisões do mantenedor:
   - o Vulkan vira padrão, com recuo automático para o software?
   - aceita a transferência do item 3?
5. Depois: retomar o N2 (U5–U8, promoção do tempo experimental, N2c), sem antecipar N8, N9 ou N10.

## 25. Fechamento do N7 (2026-10-23)

Decisão do mantenedor (2026-10-23): "configure Vulkan como preferencial com fallback seguro para software. Mantenha a
pendência AMD documentada e transfira SSAO, widescreen real e MSAA para N9". O N7 fecha com **promoção condicional**,
como o N6.

### 25.1 Vulkan como padrão

- `kRenderNativeDefault = true` (`app/main_sdl.cpp`) e `GraphicsSettings::vulkan = true`: sem `graficos.toml`, a janela
  abre no Vulkan. O `graficos.toml` (escolha do jogador no menu), `--render software|nativo` e `BS_RENDER` continuam
  valendo sobre o padrão.
- O `--headless` (diagnóstico, baterias, oráculos) continua no software, que é a referência das comparações; o
  Vulkan nele continua com `--render nativo`.
- Recuo seguro, em três camadas:
  1. **na partida, sem dispositivo:** `clamp_to` sem Vulkan devolve o preset Original **com `vulkan = false`** (antes
     herdava o padrão da estrutura); o menu mostra o renderizador em software e o motivo vai para o log;
  2. **na apresentação:** a apresentação direta que falha recua para o SDL (`fallbackToSdl`, já existente);
  3. **no jogo, perda do dispositivo:** até 3 recriações por sessão e depois o software até o fim dela
     (`render_capture`, já existente; unitário `render_capture_vulkan_failures_fall_back_to_software`).
- Defeito corrigido junto: trocar a **predefinição** no menu trocava o renderizador para o padrão da estrutura (o preset
  é uma `GraphicsSettings` nova). Agora a predefinição preserva o renderizador, como já preservava janela e V-Sync.
- Observação para quem já jogou versões anteriores: o `graficos.toml` gravado por elas tem `renderizador = "software"`
  (o padrão de antes) e continua valendo; o Vulkan se escolhe em Gráficos > Renderizador.

Validação (2026-10-23, build Release, RTX 5080, pasta de teste com os mods da Preview):

| Caso | Resultado |
|---|---|
| F1 janela sem opções | Vulkan: 428 quadros, 0 falhas, 0 recriações |
| F2 janela com `BS_VULKAN_GPU=GPU_INEXISTENTE` | "Vulkan indisponível": apresentação SDL e renderizador em software, rc 0 |
| F3 `graficos.toml` com `renderizador = "software"` | software (sem `[render-vk]`), 59,9 campos/s |
| F4 `--render software` | software |
| F5 `--headless 1200` sem opções | software (referência) |
| F6 `BS_RENDER_IR=comparar`, rota de combate | 586 renders comparados, 0,0000% de pixels diferentes |
| Unitários | 304/0; novos: padrão e recuo em `graphics_clamp_to_capabilities`, `settings_menu_renderer_default_and_preset` |

Revisão do Codex (2026-10-23, inline, `reviews/codex-20261023-093452.md`): 1 achado, **rejeitado**. [MÉDIA] "o
`--headless` não garante o software com `--render nativo` ou `BS_RENDER=nativo`": é intencional (o Vulkan headless é o
caso C2 das baterias e o modo `comparar`); só o **padrão** do headless é o software. O texto da ajuda foi reescrito para
dizer isso. Pontos verificados sem problema: precedência, `graficos.toml` com software, recuo sem dispositivo,
predefinição preservando o renderizador, testes.

### 25.2 Transferidas para o N9 (Remasterização)

Decisão do mantenedor: **SSAO**, **widescreen real (16:9)** e **MSAA** passam para o N9. Ficam com elas os itens que
dependem dos mesmos pré-requisitos: texturas HD, filtragem bilinear/anisotrópica, iluminação e sombras aprimoradas e os
presets Equilibrado e Alta Qualidade. O menu continua mostrando "em breve" e o preset Original não muda. Pré-requisitos
registrados para o início do N9: projeção do SDK (`njSetScreen` ou equivalente), marca cena/HUD por desenho na IR,
profundidade guardada depois do render pass (SSAO); projeção e HUD (16:9); alvo multiamostrado e resolve (MSAA).
Nada disso foi antecipado.

### 25.3 Pendências técnicas bloqueantes

- **GPU AMD integrada:** perda intermitente do dispositivo (`VK_ERROR_DEVICE_LOST`) no modo `comparar`. A captura de
  2026-10-24 pelo `VK_LAYER_LUNARG_crash_diagnostic`/`VK_EXT_device_fault` comprovou um `GPU fault` por `Invalid Read`,
  mas ainda não identificou qual shader ou recurso originou o endereço inválido. A hipótese de driver **não** está
  comprovada. Com o Vulkan padrão, a recuperação (recriação e depois o software) protege o jogador; a apresentação
  direta não foi testada na AMD.
- Fog e volumes modificadores contra o Flycast (antes do N8, que remove o software de referência). A rota de combate
  confirma uso real dos dois a partir do render 1127; falta uma captura Flycast pareada e a implementação fiel.
- Rotas além da de combate render a render.

Esses itens não são tratados como implementados nem escondidos. A recuperação e o recuo da AMD protegem o jogador,
mas são mitigação, não eliminação da causa. Fog e volumes são uma lacuna herdada também pelo rasterizador em software,
mas pertencem à fidelidade obrigatória do N7. Enquanto a causa AMD e a prova/correção de fog e volumes não estiverem
fechadas com hardware e oráculo suficientes, o N7 permanece em **95%**.

### 25.4 Próximo passo

O passo definido no fechamento de 2026-10-23 — retomar o **N2** para temporização, desempenho e H9 do N6 — foi
concluído em 2026-10-24 (`docs/N2-PLANO.md`, seção 14). O próximo marco é o N8, que não foi iniciado e aguarda
autorização explícita do mantenedor.

### 25.5 Correção do status (2026-10-24)

O registro anterior de 100% confundia disponibilidade funcional, protegida por fallback, com conclusão técnica. As
provas de nível 2 já aprovadas permanecem válidas — equivalência bit a bit Vulkan × software, rota de combate,
apresentação direta, 720p–4K, Validation Layers, falhas injetadas, fallback, menu, pausa, legendas, câmera e
pós-processamento —, mas não eliminam os bloqueios da seção 25.3. O N7 volta a **95%: funcional, porém não totalmente
concluído**. N8, N9 e N10 continuam não iniciados e dependem de autorização explícita do mantenedor.

### 25.6 Investigação dirigida das pendências (2026-10-24)

**AMD integrada.** A rota A1 (`passo_combate`, 1200 VBlanks, `BS_RENDER_IR=comparar`) foi repetida dez vezes sem
Validation Layers. Nove passaram com 583/583 renders idênticos; uma perdeu o dispositivo no render 470, depois que o
render 469 produziu a imagem incorreta. A recuperação concluiu a sessão, mas não corrigiu a causa. Com a camada de
diagnóstico de crash e as camadas implícitas desativadas, a falha reapareceu já no primeiro ensaio, desta vez durante a
espera da cerca do render 370. O relatório do dispositivo AMD registrou:

- `description: GPU fault`;
- faixa de endereço `Invalid Read`;
- fila gráfica/compute/transfer com a submissão incompleta;
- nenhuma evidência suficiente para atribuir o defeito ao driver.

A camada de crash informa que não consegue inserir checkpoints precisos dentro dos render passes legados usados pelo
backend; por isso o dump não determina se a leitura saiu do shader de fragmento, do resolve compute, de uma textura ou
de um storage buffer. As Validation Layers completas + sincronização, na mesma A1 e no executável novo, passaram com
zero VUID/SYNC-HAZARD e zero recriações. Isso restringe a investigação, mas não contradiz o fault de GPU e não encerra a
pendência.

**Fog e modifier volumes.** O resumo da IR passou a contar cada modo de fog e o primeiro render de uso, sem alterar a
imagem. Na rota completa de 9000 VBlanks (RTX 5080, `--render nativo`) houve 4484 renders, zero falhas Vulkan e:

| Evidência | Resultado |
|---|---:|
| desenhos com fog por tabela | 2.758.908; primeiro no render 1127 |
| desenhos com fog por vértice / `Table2` | 0 / 0 |
| desenhos sem fog | 1.123.334 |
| volumes modificadores | 76.287; primeiro no render 1127 |
| triângulos de volumes / volumes sem fechamento | 2.109.830 / 1.170 |

Portanto, fog por tabela e volumes são usados de verdade e não podem ser classificados como efeitos opcionais do N9.
O repositório não contém uma captura gráfica Flycast pareada com o render 1127; sem essa referência não há base para
implementar ou aprovar a semântica visual. A instrumentação localiza o cenário a extrair no próximo ensaio, mas não
substitui o oráculo. N7 permanece em **95%**.

Validação dirigida desta rodada: `bs_unit_tests` 306/306; rota NVIDIA 9000 VBlanks, 4484 renders e zero falhas; A1 AMD
com Validation Layers 0 mensagens/0 recriações; A1 AMD sem camadas 9/10 sem perda e 1/10 com perda reproduzida. As
provas anteriores de câmera, menus, áudio, legendas, N2, fallback e apresentação continuam válidas e não foram repetidas.

### 23.4 Câmera livre em ambientes fechados (2026-10-23, correção de jogabilidade)

Relato do jogador: em áreas abertas a câmera Livre e o movimento funcionavam; em corredores e salas, o analógico
esquerdo para a direita podia fazer o Eliot andar para a frente na imagem, ele deixava de acompanhar a vista e os
controles pareciam voltar ao original. Pedido do mantenedor: corrigir em qualquer área jogável e retirar o modo Híbrido.

**Método.** Bateria por área, reproduzível (roteiros por passo de jogo): o analógico esquerdo nas 8 direções × a vista
em 0°, 45°, 90°, 180° e 270° (giro pelo analógico direito via `BS_CAMERA_ROTEIRO` com `indice passo`), mais andar com a
vista girando e o R3. Medidas: o deslocamento real do Eliot no plano (`BS_STATE_PASSO`) e a orientação dele pela câmera
de perseguição do jogo (`CD60` no fim do segmento, que se alinha atrás do Eliot), o que separa **direção errada** de
**deslizar na parede**. Instrumentação mínima: `BS_CAMERA_LOG` ganhou a coluna `ref` (`D680`) e `BS_CAMERA_MOV_LOG`
registra cada amostra girada (entrada, vista, desvio dos cortes, referência, giro); `BS_PAD_TRACE` e `BS_RAM_WATCH`
deram a entrada que o jogo leu e `D680`/`CD60` por VBlank.

**O jogo está certo.** Em todas as áreas o rumo do Eliot é `D680` + o ângulo da entrada entregue (ex.: entrada
(−100, −80) = +51°, `D680` 322,7° → previsto 14°, real 2→13°). `D680` é a cópia do yaw da câmera do jogo no toque
(23.3); as áreas fechadas não usam outra rotina de movimento nem outra referência nas rotas medidas.

**Causa 1 — a ancoragem parava no zero.** A vista só ficava ancorada no mundo enquanto o deslocamento do rig era ≠ 0;
quando a câmera do jogo "alcançava" a vista, o deslocamento acabava e a vista voltava a seguir a câmera de perseguição,
que gira ~2°/VBlank atrás do Eliot quando ele anda. Corredor RCG02, vista girada 90°: o deslocamento caiu de 36,9° para
0 no primeiro segmento; segurando "direita", o rumo girou de 34° a 249° (o Eliot andou em círculo, sempre à direita da
imagem do momento, que girava junto). A câmera do jogo gira à mesma velocidade no cais aberto (2°/VBlank; mais longe do
Eliot, 350 × 233 unidades): o defeito existia nos dois, e aparece mais nos corredores, perto da câmera e com mais
paredes. Correção: no Livre a vista fica **sempre** ancorada (`CameraRig::absorb`); só o analógico direito, o R3 e os
cortes a mudam.

**Causa 2 — o toque nunca acabava.** A movimentação relativa guarda, durante um toque, o desvio dos cortes de câmera
(`bias`, para o rumo continuar quando a câmera corta). O toque só acabava com o analógico em **zero exato**; a zona
morta padrão do analógico esquerdo é 0 e um controle parado (ou o centro 128 do Dreamcast) dá ~0,003. O desvio de
cortes antigos (cenas, portas) ficava para sempre: no corredor RMT00, 18 994 unidades (104°), e **todas** as direções
saíam giradas ~104° (0 de 42 certas). Correção: o toque acaba abaixo de 0,05 (`kReleased`).

**Revisão do Codex** (`reviews/codex-20261023-144759.md`, inline): 2 achados, **aceitos**. [ALTA] depois de cena,
suspensão ou troca de modo a linha de base (`lastYaw`) ficava velha e, com a ancoragem sempre ativa, a primeira
montagem criaria um deslocamento não pedido: a base agora é invalidada nesses casos e a primeira montagem só captura.
[MÉDIA] o primeiro limiar (0,15) deixava de girar inclinações leves válidas: o toque acaba abaixo de 0,05 (bem acima do
ruído de 0,003) e acima disso a entrada é girada.

**Híbrido retirado.** Modos: Original Dreamcast e Câmera Livre. `controles.toml` com `modo = "hibrida"` vira Livre
(as outras preferências ficam); `retorno_s` é ignorado; `--camera hibrida` vira `livre`. O código exclusivo do
Híbrido (retorno automático e a opção do menu) saiu; o R3 continua.

**Validação** (build Release; analógico esquerdo, vista girada pelo rig real):

| Área | Ambiente | Orientação certa | Deslocamento |
|---|---|---|---|
| B1 cais RCG00 (`passo_cgate` 2660) | aberto | 41/42 | 34 ok, 6 colisões, 2 bloqueados, 0 errados |
| B2 corredor RCG02 (LOAD do g1) | corredor estreito | 41/42 | 29 ok; os 10 com desvio são deslizamento (orientação certa) |
| B3 sala RCG90 (sessão do mantenedor, 1186) | sala fechada pequena | 41/42 | quase tudo encosta em parede (orientação certa) |
| B4 corredor RMT00 (sessão, 1822) | corredor | 23/24 (18 em cena) | 16 ok (antes: 0) |
| B5 RMT00, área do combate (sessão, 3604) | corredor com combate | 41/42 | 33 ok, 8 colisões |
| B6 REL01 (sessão, 6559) | sala interna logo após o externo | 18/24 (6 bloqueados, 18 em cena) | — |
| B7 RCG03 depois da porta (`passo_heliporto` 7790, vista girada antes, no corredor) | transição interno → externo | 41/42 | 34 ok, 8 colisões, 0 errados |

O único "fora" das áreas livres é o "girando-frente", em que a vista gira 90° durante o segmento: no fim o Eliot fica
a +5,6° da vista nas quatro áreas medidas. O caso relatado (vista girada e analógico para a direita) andou para a
direita da vista em todos os ângulos e áreas. Câmera originalmente fixa: nenhuma zona assim nas rotas e gravações
disponíveis (todas as salas medidas usam câmera de perseguição; os cortes caem nas cenas). Fica como cobertura pendente.

| Prova | Resultado |
|---|---|
| Original Dreamcast: executável novo × anterior, `passo_combate` | estado por passo idêntico (4 317 passos) |
| Combate e mira: `passo_combate` Original × Livre com a vista girada ~80° antes do combate | estado do jogo idêntico por passo (4 317; 634 montagens giradas; dano de 100 para 40) |
| Vulkan × software (B1) | estado e montagens idênticos |
| Determinismo (B1 repetida) | idêntica |
| Gravação na janela (vista girada pelo rig, 359 amostras giradas) × reprodução headless sem câmera | iguais em 5 683 passos |
| Save/Load | LOAD do save g1 nas baterias B2 (3/3) |
| Unitários | 304/0 (Híbrido migrando para Livre, ancoragem que não para no zero, menu com dois modos) |

### 23.5 Câmera Livre na troca de cenário (2026-10-23, correção de jogabilidade)

Relato: atravessando uma transição com o analógico esquerdo para a frente, o Eliot passava a andar para a esquerda na
área nova até a direção ser corrigida à mão.

**Reprodução** (`tools/dev/n7_camera_transicao.py`): `passo_heliporto` até a porta RCG02 → RCG03, vista girada ~45° no
corredor, analógico esquerdo segurado para a frente desde antes do A da porta até o exterior. **Causa medida:** o
desvio dos cortes (`MoveLatch::bias`, que mantém o rumo num corte de câmera dentro da área) somou o salto da vista da
troca de cenário: 41 134 unidades (~134°). O jogo mantém `D680` (47 910) enquanto o analógico está seguro, e a vista
nova recomeçava na câmera do jogo do exterior (yaw 0): o giro entregue ficava em 37° e o Eliot andava ~105° fora da
frente da vista (mediana de 3 trechos).

**Correção:** o adaptador nativo de arquivos (`transitional/gdfs_adapter`) conta as cargas de sala (a leitura do início
de um `R*.BIN`, com dados): `room_loads()`. Na montagem seguinte a uma carga, a câmera trata a troca como corte (a vista
recomeça na câmera do jogo da área nova, mesmo quando a câmera nova fica perto da antiga) e `relative_input` zera o
desvio: o movimento acompanha a vista nova **sem soltar o analógico**. Cortes dentro da área continuam com o rumo. Nada
por sala, nenhum ângulo fixo, o modo Original não é tocado (os slots nem são instalados nele).

| Prova | Resultado |
|---|---|
| `n7_camera_transicao.py` (porta RCG02 → RCG03, analógico seguro) | antes: desvio 41 134, ângulo deslocamento × frente da vista mediana 105°; depois: desvio 0, mediana 0° (trechos 31, 81, 25, 5, 1, 0, 0, 0: o Eliot virando para a frente nova), a carga contada na montagem do corte |
| Original, mesma rota | nenhuma amostra girada |
| Original, `passo_combate`: executável da `dist` × novo | estado por passo idêntico (5 817 passos) |

**Revisão do Codex** (`reviews/codex-20261023-212005.md`, junto com as legendas dos filmes): aceitos: a carga só conta
com dados lidos; o teste amarra a contagem à montagem do corte (coluna `salas` no `BS_CAMERA_LOG`). **Limite
registrado:** só o provedor nativo de arquivos conta as cargas; com `--arquivos legado` (recuo até o N8) a troca de
cenário cai no comportamento anterior.

### 18.6 Correção definitiva das legendas das falas (2026-10-23)

Relato do jogador: (1) legendas vários segundos antes da fala, sumindo mais ou menos na hora certa; (2) duas falas
seguidas, de personagens diferentes, numa legenda só; (3) pedido de exibir cada fala sozinha e identificar quem fala
quando houver evidência.

**Causas comprovadas.**
1. *Antecipação, nos pacotes:* o ASR (faster-whisper) estica a primeira palavra de um segmento sobre o silêncio
   anterior. Contra a energia do próprio áudio de cada fala, 72 linhas do 0.2.0 começavam > 0,5 s antes da voz (até
   7,08 s); na reprodução, até 12 s quando a linha começava na sobra da fala anterior (`voz/e083`: nenhum som entre 17,6
   e 29,5 s, e a linha estava em 17,57 s). Na sessão gravada do mantenedor, 12 de 17 legendas entravam > 100 ms antes
   da voz (a pior, 7,07 s).
2. *Antecipação, no jogo:* o tempo da legenda era o do mixer, e o som sai do dispositivo depois da fila da saída (na
   janela, XAudio2: 133–216 ms); além disso, o tempo decorrido negativo virava 0, então a linha que começa em 0 aparecia
   no VBlank do `Started`, antes do som.
3. *Falas misturadas, nos pacotes:* as linhas só eram divididas por tamanho (2×42), nunca nas pausas: 48 linhas cobriam
   duas falas separadas por silêncio (até 11,9 s). O jogo não junta ocorrências diferentes: o MW WAV do jogo tem um
   handle só, e o `VoicePlayer` corta a fala e a cauda anteriores a cada `start`. Nas execuções medidas, 0 quadros com
   falas diferentes na tela ao mesmo tempo.
4. *Identificação:* nem o jogo nem o catálogo têm um metadado confiável de quem fala em cada linha.

**Correções.**
- Pacotes (en 0.3.0, pt-BR 0.2.0, locais): `tools/dev/legendas/ressincronizar.py` e `sincronia.py` refazem os tempos
  pela voz que cada linha cobre e dividem em partes nas pausas, sem mudar texto, revisões nem chaves
  (`docs/LEGENDAS-PENDENCIAS.md`, seção "Sincronia").
- Jogo: a legenda segue o quadro do mixer que está **saindo do dispositivo** (blocos aceitos pela saída + fila atual;
  descartes não contam; a reserva de silêncio fica antes do primeiro bloco), sem deslocamento fixo; tempo negativo =
  nada na tela; cada legenda (ocorrência + linha/parte) entra e sai sozinha; a troca de idioma no meio da fala continua
  a fala no idioma novo; evento `Requested` e o diagnóstico `BS_LEGENDA_LOG`; campo opcional `personagem` e a opção
  "Identificação do personagem" (indisponível enquanto nenhum pacote disser quem fala).
- Revisão do Codex (`reviews/codex-20261023-160518.md`): [MÉDIA] o cálculo "misturado − fila" erra com blocos
  descartados → **aceito**, trocado pelo mapa dos blocos aceitos; [BAIXA] a troca de idioma deixava o log incompleto e
  reiniciava as ocorrências → **aceito**, corrigido.

**Medições.**

| Medida | Antes | Depois |
|---|---|---|
| Pacote en: linhas > 0,5 s antes da voz / > 1 s | 72 / 5 | 3 (máx. 0,61 s) / 0 |
| Sessão gravada (17 legendas), entrada − início audível | −7 065 a +103 ms; 12 antecipadas > 100 ms | −49 a 0 ms; 0 antecipadas |
| Janela (XAudio2): entrada depois do início no mixer | 0 ms (antes do som) | 150 ms, com a fila medida de 133–150 ms |
| Falas diferentes ao mesmo tempo na tela | — | 0 quadros |

O −41 ms típico é a folga de 50 ms antes da voz (`FOLGA_INI`), dentro de um VBlank de resolução. A saída fica até
~300 ms depois do fim da voz (`FOLGA_FIM`); quando duas falas não têm silêncio entre si, a anterior sai no instante em
que a seguinte entra.

**Testes em gameplay real** (sessão `sessao-20261021-145006`, falas `e025a`, `e025b`, `e005b`, durante a música):
fala isolada; falas seguidas de personagens diferentes e da mesma pessoa, com e sem silêncio entre elas (uma de cada
vez); fala com silêncio inicial longo (`e005b`, a linha que entrava 7 s antes); fala interrompida (`e005`, `e005env`:
nada sobra); pausa e retomada pelo menu na janela; troca pt-BR → Desativadas → English no meio da fala; inglês e
pt-BR com o mesmo tempo; `n7_legendas.py` APROVADO em software e Vulkan (763 quadros com fala, 442 sem nada na
tela); filme NEWGAME com pt-BR, inglês e nenhuma (legenda só na faixa de baixo; pt-BR diferente do inglês). Os mesmos
testes no executável da `dist`. Duas vozes realmente simultâneas não acontecem no jogo (handle único).
