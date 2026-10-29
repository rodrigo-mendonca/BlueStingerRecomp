# N8.7: IR nativa, fallback software e retirada do PVR

**Situação (2026-10-28): bloqueada pelos pré-requisitos do plano.** Inventário feito, nenhum corte executado. Avaliado
pela autorização contínua do mantenedor até o fim do N8. Referência: o fechamento do N8.6.

## 1. Pré-requisitos (`docs/N8-PLANO.md`, N8.7, item 2)

> "N8.2, D-N8-2, evidência N7 de fog/volumes e software-IR aprovado."

| Pré-requisito | Situação |
|---|---|
| N8.2 | concluído (2026-10-26) |
| D-N8-2 (fonte da IR) | decidida no plano: **(b) listas do Kamui em buffers nativos**, com as APIs Kamui/Ninja nativas no N10 |
| evidência N7 de fog e volumes | **ausente.** O N7 (`docs/N7-PLANO.md`, seções 16 e 24 a 26) registra fog por tabela e volumes modificadores como "pendente de fidelidade". São usados de verdade: 2 758 908 desenhos com fog por tabela e 76 287 volumes, a partir do render 1127. Nem o rasterizador em software nem o Vulkan os aplicam como o console, e não há quadro pareado contra o Flycast. O N7 permanece em 95% e **não pode ser reaberto** (restrição do mantenedor) |
| software-IR aprovado | **ausente:** não existe backend de software sobre a IR nativa. O rasterizador em software atual (`pvr_render`) lê TA/VRAM/registradores do PVR. Ele é requisito permanente do produto e o recuo do Vulkan, e não pode ser retirado |

O próprio plano fixa a consequência (seções 1.2, 2 e "riscos"): "fog/volumes bloqueiam o corte gráfico N8.7" e "o
fallback software não pode ser retirado".

## 2. Inventário do caminho gráfico no produto (somente leitura)

**Sonda `BS_HW_PROBE`** (validação, `newgame_area1`, 6 000 VBlanks):

| Região | Funções | Acessos |
|---|---|---|
| store queues (`sh4_sq`) | 184 | 79 040 019 |
| FIFO do TA (`ta`) | 13 | 2 691 836 |
| registradores do PVR (`pvr_regs`) | 221 | 376 793 |
| VRAM 32 bits | 130 | 2 247 680 |
| VRAM 64 bits | 28 | 430 112 |
| DMAC (canal 2 para o TA) | 12 | 35 838 |
| DMA do PVR | 8 | 8 |

**Módulos no produto** (papel `simulacao`, cerca de 2 000 linhas):
- `memory/sh4_sq` e `sh4_dmac`: transporte das listas;
- `pvr/ta`: decodificador da FIFO de polígonos e textura direta. O conversor YUV já é erro explícito no produto
  (N8.5);
- `pvr/pvr_regs`, `pvr_render` (o rasterizador em software), `pvr_texture`, `pvr_video`, `vram`;
- `pvr/holly_intc`: interrupções do VBlank e do render. Também serve o barramento de periféricos e a área de som;
- `pvr/holly_sb`: system block. Guarda o drive, o G1 e o DMA do G2 (N8.3, N8.6);
- `transitional/render_capture`: TA/VRAM/PVR para a `CommandList`, a IR do Vulkan.

## 3. O que o corte exigiria (para quando os pré-requisitos existirem)

1. **Produtor nativo da IR:** as listas do Kamui em buffers nativos, interceptadas antes das store queues e do DMAC.
   As texturas e o framebuffer viriam da memória do guest, sem a VRAM simulada.
2. **Backend software-IR:** o rasterizador em software consumindo a mesma IR do Vulkan, aprovado contra o software
   atual (quadros iguais nas rotas).
3. **Fog por tabela e volumes:** implementação fiel nos dois backends e quadros pareados contra o Flycast (o oráculo
   gráfico). É o trabalho pendente do N7.
4. **Migração por classe**, com comparação tripla (software atual, software-IR, Vulkan) e Validation Layers. Só então
   SQ/DMAC/TA/VRAM/PVR passam ao papel de validação e a catraca os exclui do produto.

São alterações grandes (produtor da IR, segundo backend, fidelidade gráfica), fora do que pode ser feito com
equivalência provada sem reabrir o N7.

## 4. Decisão

Nenhum corte gráfico no N8.7: o bloqueio é real e reportado, sem contornar a validação. O produto continua com o caminho
gráfico atual (Vulkan padrão, com recuo para o software), preservado e coberto pelas 16 referências e pelas suítes.

Ficam como pendências do N8 para o mantenedor decidir:
- reabrir o N7, para a fidelidade de fog e volumes contra o Flycast;
- autorizar o backend software-IR e o produtor nativo da IR;
- ou aceitar o N8 fechado sem o corte gráfico, com uma "decisão explícita compatível", como prevê o N8.8.

## 5. Implementação autorizada (2026-10-28)

O mantenedor autorizou a implementação do N8.7 antes do fechamento do N8.8, com a AMD como primeira prioridade. O
diagnóstico somente leitura está em `docs/N8-7-DIAGNOSTICO.md`.

### 5.1 Arquitetura atual (produto)

```
código AOT (SDK Kamui) --SQ/DMAC canal 2--> FIFO do TA (pvr/ta: lista de TaParam) --STARTRENDER-->
  pvr_render: Parser (o único decodificador das listas) --+--> Raster (software; texels da VRAM) --> VRAM (FB_W_SOF1)
                                                           +--> PvrCaptureSink (render_capture) --> CommandList (IR)
                                                                  --> Vulkan (modo nativo, substitui o Raster)
registradores (pvr_regs; quadro lido por render_capture) · texturas na VRAM (vram, lidas pelo TCW)
fim de render e VBlank: holly_intc (tempo lógico) · imagem: pvr_video lê FB_R_SOF1 da VRAM
```

### 5.2 Bloqueios concretos e relação com o N7

| Bloqueio | Situação | N7 |
|---|---|---|
| perda do dispositivo na AMD | **causa comprovada e corrigida** (5.4) | bloqueio próprio do N7 |
| fog por tabela | falta implementação nos dois backends e oráculo | bloqueio próprio do N7 |
| volumes modificadores (sombra de um volume) | faltam o bit de sombra e `FPU_SHAD_SCALE` na IR e a implementação | bloqueio próprio do N7 |
| software-IR | o `Raster` lê o TA e a VRAM. O `pvr_render_triangles` (prova do N7a) já rasteriza a partir dos triângulos | só N8.7 |
| produtor nativo | o decodificador vive dentro do caminho do TA e das store queues | só N8.7 |

Resolver fog, volumes e AMD no N8.7 resolve os bloqueios técnicos do N7. A mudança do percentual do N7 continua
dependendo do fechamento formal dele: as provas ficam registradas, e o percentual só muda por decisão registrada.

### 5.3 Sequência

1. **AMD** (5.4).
2. **Contrato da IR:**
   - o bit de sombra do PCW (bit 7) e o bit "dois volumes" (bit 6) por desenho;
   - `FPU_SHAD_SCALE` e o `FOG_CLAMP` do quadro;
   - a instrução e o recorte de cada volume.

   Só a captura; o modo `conferir` continua com 0 divergências. Dois volumes: o jogo não usa (`N7-PLANO` seção 12), e um
   desenho com o bit 6 vira erro explícito, nunca é descartado em silêncio.
3. **Medidas:**
   - tabela e densidade da fog no render 1127;
   - desenhos com o bit de sombra;
   - uso da lista 3.
4. **Software-IR:** o rasterizador em software consome a `CommandList`, com as texturas da tabela de texturas da IR
   (conversores nativos) e não da VRAM. Tem de ser bit a bit igual ao software atual: em todo render das rotas, nas
   cenas de fidelidade e nas 16 referências.
5. **Fog e sombras** no software-IR e no Vulkan: cenas sintéticas com igualdade exata entre os dois, e o oráculo de
   pixels do Flycast (`rend.EmulateFramebuffer`, o framebuffer lido pelo GDB) nos renders com fog e sombra.
6. **Serviço gráfico nativo** (o padrão do N8.3–N8.6):
   - a fronteira do guest (FIFO do TA, registradores do PVR, área de texturas, store queues para essas áreas, DMAC do
     canal 2) atendida com o contrato medido;
   - a lista decodificada direto para a IR;
   - as texturas num heap endereçado pelo TCW: memória do guest, sem comportamento de hardware;
   - o estado do quadro numa estrutura nativa;
   - o fim de render como evento do tempo lógico;
   - fora do contrato, erro explícito.
7. **Corte:**
   - `ta`, `pvr_regs`, `vram`, `pvr_texture`, o `Raster` sobre o TA, `sh4_sq`/`sh4_dmac` genéricos e
     `transitional/render_capture` vão para a validação;
   - a catraca os exclui do produto;
   - o software-IR fica no produto como recuo do Vulkan.

### 5.4 AMD: causa comprovada e correção

Os experimentos controlados (`tools/dev/n87_amd.ps1`; logs em `%LOCALAPPDATA%\BlueStingerRecomp\n8-0\n87_amd*`):

| Configuração (rota A1, 1 200 VBlanks, AMD integrada) | Rodadas com perda do dispositivo |
|---|---|
| GPU isolada (`--render nativo`, sem software) | 11/30 |
| `comparar` com o software sequencial | 9/30 |
| GPU isolada, sem o resolve do autosort | 1/12 |
| GPU isolada, sem os desenhos do autosort | 4/12 |
| GPU isolada, todos os desenhos sem textura | 2/12 |
| GPU isolada, sem nenhum desenho (só limpeza, cópias e leitura de volta) | 0/12 |
| GPU isolada, sem o caminho exato do fragmento (que lê o buffer de triângulos) | 0/12 |
| GPU isolada, sem a profundidade crua | 4/12 |

Conclusões:
- **A carga da CPU não é a causa.** A GPU isolada falha na mesma taxa que o `comparar`.
- O registro de 2026-10-24 ("não aparece no modo nativo") vinha de 2 rotas só, e o render da falha sai com a imagem do
  anterior.
- A camada de crash (`VK_LAYER_LUNARG_crash_diagnostic`, terminal sem elevação) deu o endereço da falha:
  `0x309101000`, "Invalid Read", numa faixa sem nenhum objeto vinculado no histórico.
- As falhas só acontecem nos renders em que o buffer de triângulos é realocado (1, 370 e 469).
  - O código liberava o buffer, criava outro e só regravava o descritor se o handle mudasse.
  - O driver da AMD às vezes devolve **o mesmo handle**.
  - O descritor continuava na memória liberada, e o fragmento exato lia uma página sem mapeamento. O
    `robustBufferAccess` não protege contra isso.
- Correlação em 32 rodadas instrumentadas: 6 tiveram o handle repetido e as 6 falharam; 26 não tiveram e nenhuma falhou.

**Correção** (`vk_device.cpp`, `record`): o descritor é regravado em toda realocação, e o handle não é mais comparado.
O NVIDIA nunca reaproveitou o handle, por isso nunca falhou.

**Testes:**
- `render_vulkan_triangle_buffer_growth_rewrites_descriptors`: uma grade com 76 800 triângulos força a realocação; o
  teste confere a regravação, a imagem e o quadro pequeno depois. Passa na NVIDIA e na AMD;
- a reprodução da rota A1 na AMD com o exe corrigido: **0 perdas do dispositivo em 90 rodadas** (GPU isolada 0/30,
  `comparar` com o software paralelo 0/30, sequencial 0/30), contra 11/30 e 9/30 antes.

### 5.5 Software sobre a IR, contrato da IR, fog e volumes

**Renderizador em software sobre a IR** (`native/render/soft`):
- `soft_raster` é o rasterizador de referência do N1, movido de `runtime/pvr/pvr_render.cpp` sem mudar as contas. Só a
  origem dos texels passou para uma `TexelSource`;
- `soft_device` é um `IRenderDevice`, como o Vulkan: recebe a `CommandList`, as texturas convertidas e devolve a imagem.
  A volta da IR para as palavras do hardware é a mesma do modo `conferir`;
- o `render_capture` ganhou o modo `Software`, que é o padrão do software no `GameHost`. O software sobre a IR também é
  o recuo do Vulkan, sempre com o motivo no log.

**Contrato da IR:**
- o bit de sombra do PCW (bit 7) por desenho;
- `FPU_SHAD_SCALE` no quadro;
- por volume: o descarte de faces, se foi fechado, e o fechamento por OU;
- os polígonos de "dois volumes" contados à parte; o jogo não usa nenhum.

**Medidas na rota de combate inteira** (27 149 renders):

| Medida | Valor |
|---|---|
| desenhos com o bit de sombra | 6 198 929, a partir do render 370 (`FPU_SHAD_SCALE 0x180`: escala 128/256) |
| volumes | 297 065: todos de inclusão; 0 exclusão, 0 por OU, 0 descarte misturado, 0 sobre translúcidas, 5 069 sem fechamento |
| fog por tabela | 8 835 493 desenhos a partir do render 14 620; `FOG_DENSITY 9C0E`, `FOG_COL_RAM 7CB3C2`, 52 entradas não nulas, máximo 255 |
| dois volumes | 0 |

**Semântica**, lida no Flycast só como comportamento, sem código copiado:
- **Fog por tabela:** z = densidade × 1/w, limitado a [1, 256). O índice tem 16 entradas por oitava. Os dois valores da
  entrada são interpolados pela fração. A cor final mistura com `FOG_COL_RAM` depois da textura, do offset e do clamp.
- **Volumes da lista 1:** depois das opacas e do punch-through, cada triângulo à frente da profundidade (teste
  estrito) inverte a paridade do pixel, ou a liga por OU a partir de `or_from`. No fechamento, a área do volume soma o
  estado ao resultado, por OU na inclusão e por E na exclusão. Os pixels dentro, cujo polígono visível tem o bit de
  sombra, ficam com a cor × escala / 256.

**Vulkan:**
- dois passes: A com as opacas e o punch-through, e um anexo `R8_UINT` com o bit de sombra do polígono visível; B com
  as translúcidas em ordem e o autosort;
- entre os passes, `volumes.comp` aplica os volumes por pixel com as contas do software, a partir de listas por tile de
  32 × 32 feitas na CPU;
- a fog fica no `pvr.frag`, com a tabela num buffer do quadro.

**Provas:**
- cenas de fidelidade novas: `modificador_opaco`, `modificador_inclusao_exclusao_ou` e `fog_tabela`. Os valores
  esperados saem de um avaliador independente, em precisão dupla, no teste;
- software sobre a IR × Vulkan, imagem inteira nos 32 bits: 10 cenas iguais na NVIDIA e na AMD, com e sem a
  profundidade crua (`BS_VK_SEM_PROFUNDIDADE_CRUA=1`);
- Validation Layers completas mais sincronização: 0 mensagens nas duas GPUs;
- `conferir` na rota de combate inteira: a IR, o software sobre a IR e o caminho do TA concordam em todos os 27 149
  renders, com fog e volumes;
- `comparar` na rota de combate inteira (NVIDIA): 27 149 renders, RGB idêntico em todos (PSNR mínimo 99 dB). Até 1
  nível de diferença só no alfa, em menos de 0,00005% dos pixels. **Já existia antes do N8.7:** o exe anterior (a Preview
  do `c6d8a8e`) dá o mesmo resultado na mesma rota. O framebuffer do jogo (565) não guarda alfa. Custo de GPU por quadro:
  1,21 ms antes, 1,44 ms com fog e volumes;
- `BS_RENDER_SEM_FOG_VOLUMES=1` desliga os dois efeitos nos dois renderizadores. Serve de diagnóstico: prova que as
  únicas mudanças de imagem são esses dois efeitos (5.7).

### 5.6 O corte: o que saiu do produto

**Saiu**, com equivalência provada (5.5 e 5.7):
- o rasterizador sobre os triângulos do TA (`pvr_render_ta.cpp`);
- a decodificação de texels da VRAM (`pvr_texel.cpp`);
- o conversor YUV (`ta_yuv.cpp`, recusado desde o N8.5).

Os três vão para o papel de validação: a referência das comparações, o modo `conferir` e o caminho antigo.

**Como o produto funciona agora:**
- desenha só pela IR: o software sobre a IR, ou o Vulkan com o software sobre a IR de recuo;
- um render que a IR não desenhou é erro explícito (`sh4_fatal`), nunca um recuo para o caminho do TA;
- sem conversão nativa para uma textura, o render falha com o motivo. A paleta de 8 bits, com e sem mipmap, ganhou
  conversão nativa (`convert_pal8`). O YUV, o bump e a paleta de 4 bits com mipmap não são desenhados nem pelo
  decodificador do TA;
- recusa `BS_RENDER_IR` e `BS_RENDER_IR_BMP` (saída 2), que são os modos que comparam com o caminho do TA.

**Catraca `n8_produto_papeis`:**
- os três objetos só na validação: produto com 35 objetos, validação com 46;
- o mapa do linker do produto sem os símbolos `pvr_texel`, `pvr_render_ta`, `pvr_render_triangles`, `yuvConvert` e
  `VramTexels`;
- `soft_raster` e `soft_device` obrigatórios no produto;
- as recusas acima.

Antes de tudo isso, o link do produto já falha se algo referenciar a referência gráfica.

**Unitários:**
- produto 317/0: os testes que comparam com o caminho do TA ficam só na validação;
- os testes de render e de formatos passam pela IR no produto e pelo caminho do TA na validação;
- validação 331/0.

**Fica no produto**, como fronteira do guest com o contrato medido, sem rasterização nem decodificação simulada:
- os registradores do PVR, com o SPG e o fim de render no tempo lógico;
- a FIFO de parâmetros do TA e o caminho direto de textura;
- a memória de vídeo (texturas e framebuffer);
- a saída de vídeo;
- o decodificador das listas, que alimenta a IR.

Tirar essa fronteira exige produzir a `CommandList` interceptando o Kamui, com as listas em buffers nativos
(D-N8-2 b), sem passar pela FIFO, pelas store queues e pela VRAM. É uma engenharia reversa grande, sobre 184 funções que
usam as store queues, 13 que escrevem na FIFO e 221 que tocam os registradores, que não cabe com equivalência provada
neste ciclo. **Fica como pendência para decisão do mantenedor.** O N8.7 não é declarado concluído por isso.

### 5.7 Equivalência com as 16 referências do N8.0

| Rodada (exe com o corte) | Resultado |
|---|---|
| `--ambiente BS_RENDER_SEM_FOG_VOLUMES=1` | **16/16 iguais**, com as 179 exceções de SPC e nenhuma nova |
| com fog e volumes | 14 diferentes **só no artefato `quadros`**. Estado por passo, arquivos, áudio, legendas, cartão, eventos e relógios são iguais. `passo_load_opcoes` (só menus) é igual. A primeira diferença é o primeiro quadro de jogo 3D de cada rota |

O que isso prova:
- o corte (TA, VRAM e YUV fora do produto) não muda nada;
- o rasterizador movido, o software sobre a IR e o modo `Software` reproduzem o N8.0 bit a bit;
- a única mudança de imagem é a fog e a sombra.

A contagem de linhas difere em algumas rotas (por exemplo, 29 545 × 29 548) porque o artefato registra as **trocas** de
quadro por passo: com a sombra, quadros seguidos que eram iguais passam a diferir.

As referências do N8.0 ficam intactas, com os efeitos desligados como diagnóstico. O conjunto novo,
`analysis/n8/referencias_n87`, é capturado com o exe final e os efeitos ligados e vira a base dos quadros daqui em diante.

### 5.8 Oráculo do Flycast (sombra)

Rota `passo_load_slot1`, carregando o save do cartão `vmu_g1` (`flycast_oracle.py quadro --vmu`). O Flycast roda com
`rend.EmulateFramebuffer`. Para cada passo, o quadro do recompilado é o da faixa de VBlanks do mesmo passo, com e sem os
efeitos (`BS_RENDER_SEM_FOG_VOLUMES=1`).

| Passo | Pixels que a sombra muda | Mais perto do Flycast com o efeito | Observação |
|---|---|---|---|
| 1500 | 0 | n/a | menu, sem volumes |
| 2500 | 5 677 | 48 % | a fase da caminhada difere do Flycast (alinhamento da rota) |
| 3000 | 5 834 | **97,5 %** | mesma pose: a sombra é praticamente igual |
| 3500 | 5 785 | 48 % | fase da caminhada diferente |
| 3700 | 6 271 | **74 %** | |

Nas imagens ampliadas, a sombra do Eliot tem a mesma forma, posição e intensidade da do Flycast. Onde a pose coincide
(passo 3000), o erro médio nos pixels da sombra cai de 19,3 para 3,3. Nos passos com 48 %, a diferença é a pose do
personagem, não a sombra.

Esta rota não usa fog por tabela. A fog fica validada pela fórmula (seção 3), pelas cenas de fidelidade, com o software
igual ao Vulkan, e pelas medidas da rota de combate. Ainda falta um quadro de fog pareado com o Flycast.

**Correção no oráculo:** a primeira sessão ficou 4 h parada. O stub GDB do Flycast respondeu ao `?` e nunca ao primeiro
`s` do boot, e a leitura não tinha prazo. Agora a partida tem prazo de 120 s, com até três sessões novas.

### 5.9 AMD com o exe final

`tools/dev/n87_amd.ps1 -Casos iso,par16,seq -N 30` com o exe de validação do commit do N8.7 (as duas passagens do Vulkan e
o compute dos volumes), sozinho na máquina: **0 de 90 rodadas com falha**, 583 quadros por rodada e nenhum dispositivo
recriado.
