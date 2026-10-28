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
