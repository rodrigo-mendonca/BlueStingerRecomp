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
