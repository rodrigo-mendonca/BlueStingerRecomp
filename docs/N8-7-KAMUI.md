# N8.7: substituição da fronteira gráfica guest por interceptações do Kamui (proposta)

Desenho técnico autorizado pelo mantenedor em 2026-10-28 e **aprovado no mesmo dia**, com as decisões da seção 10. A
implementação segue a ordem obrigatória da seção 10.

Fontes da evidência:
- sonda do N0 (`analysis/hwscan_sonda`: `hw_manifest.tsv` com as contagens dinâmicas por PC, `callgraph.tsv`, `sdk_funcs.tsv`);
- disassembly do `1ST_READ.BIN` (`bs-disasm`);
- código do runtime (`runtime/pvr`, `runtime/memory`, `runtime/cpu/irq_bridge`);
- inventário do `docs/N8-7-DIAGNOSTICO.md`.

Marcação: **[C]** confirmado (disassembly, sonda ou código); **[H]** hipótese, a confirmar na fase 0 (seção 8).

## 1. Inventário: Kamui e as funções que tocam o hardware gráfico

O binário não tem símbolos. Os nomes abaixo são **papéis** deduzidos do código e dos acessos, não nomes do SDK.

### 1.1 Camada de hardware do Kamui (HAL), `0x8C1C59A4`–`0x8C1C5CE0` [C]

São funções pequenas, e todo acesso ao PVR fora do caminho de render passa por elas.

| Endereço | Papel | Evidência | Chamadas |
|---|---|---|---|
| `8C1C59A4` | `reg_write(off, v)`: `*(0xA05F8000 + off) = v` | disassembly (3 instruções); a sonda vê ~150 registradores gravados, incluindo a tabela de fog `8200`–`83FC` | 23 diretas, 8 dinâmicas, 2 de cauda (22 chamadores) |
| `8C1C59D6` | `reg_read(off)` | disassembly | 5 chamadores |
| `8C1C59AE` | write-back do cache em `[r4, r4 + r5)`, de 32 em 32 bytes (chama `8C1C56CE`) | disassembly | HAL |
| `8C1C59DC` | cópia para a VRAM: `memcpy(0xA5000000 + off, src, n)` (`8C1C56BE`) | disassembly | 1 (`8C1C5D46`) |
| `8C1C59F2` | cópia da VRAM | disassembly | **nenhum chamador** |
| `8C1C5A08` | carga de textura: com o endereço alinhado e as interrupções liberadas, write-back + DMA (`8C1C1E16`) para `0x11000000 + off` (caminho direto de textura do TA), repetido enquanto o retorno for `0x100`; senão, `memcpy` para `0xA4000000 + off`; espera opcional (`8C1C21BC`) | disassembly | 5 diretas, 2 de cauda (`8C18D07E`, `8C18D14C`, `8C18DA40`, `8C18DA92`, `8C1C3860`) |
| `8C1C5AE0` | cópia + write-back (outro destino) | disassembly | 1 (`8C1C3860`) |
| `8C1C5B04` | tabela de `{off, v}` → `reg_write` | disassembly | **nenhum chamador** |
| `8C1C5B36` | instala o vetor (VBR + `0x600`) e o estado das interrupções | disassembly | 1 (`8C1C48C4`) |
| `8C1C56BE` | `memcpy` (a cópia para a VRAM) | sonda: 2,1 M escritas na `vram32`, 0,9 M na `vram64` | 9 chamadores |

### 1.2 Render, apresentação e interrupção [C nos acessos, H nos papéis]

| Endereço | Papel | Acessos medidos |
|---|---|---|
| `8C1C53E2` | **tratador de interrupção do Holly do Kamui**. É chamado **dinamicamente** pelo despachante `8C1982F4`, que vem da ponte `8C00FA00` | `ISTNRM`/`ISTERR`: 24 mil leituras e escritas; `CHCR2`: 8 124 |
| `8C1C4EC6` | dentro do tratador: dispara o render seguinte e troca o quadro exibido | `810C`; chama `4914`, `4A2C`, `4C9E`, `1DB4` |
| `8C1C4C9E` | início do render | `STARTRENDER`, `PARAM_BASE`, `REGION_BASE`, `FB_W_*` (10 registradores, escrita direta) |
| `8C1C4914` | troca do quadro exibido | `FB_R_CTRL`, `FB_R_SOF1/2`, `VO_CONTROL` |
| `8C1C512E` | marca no framebuffer | `vram32` em `A5000000` e `A5400000` (191 mil); `8138` |
| `8C18BC3C` | ponto do quadro do Kamui no render (chamado pela Ninja `8C173044`). Lê `TA_ITP_CURRENT`, grava o polígono de fundo na VRAM e chama `8C18BB84`, que leva o buffer de vértices ao TA pelo DMA (`8C1C1E16`) | CCN, SQ, TA, TMU |
| `8C18D276` | carga de textura pelo DMA (+ `reg_write` + espera) | chamada por `8C1769E6` |
| `8C1C1D4C`, `8C1C1DB4`, `8C1C1E16`, `8C1C202C` | DMA do canal 2: `SAR2`/`DAR2`/`DMATCR2`/`CHCR2`, `SB_C2DSTAT`/`C2DLEN`/`C2DST` | `1E16`: 8 124 chamadas, ~1 por quadro |

### 1.3 Ninja e jogo com acesso direto (fora do Kamui) [C]

| Endereço | Papel | Acessos |
|---|---|---|
| `8C172642` | laço do quadro da Ninja (chamado pelo jogo em `8C08BB40`): sondagem do `ISTNRM` e as chamadas da API Kamui do quadro | 474 535 leituras do `ISTNRM` |
| `8C172620` | leitura do `SPG_STATUS` | 75 mil |
| `8C173C58` | carga da paleta (`PALETTE_RAM 005F9000`–`903C`) | escrita direta |
| `8C08EFF8`, `8C08F332`, `8C08F04E`, `8C08F0E0`, `8C08F450` | jogo/Shinobi: `SPG_STATUS`, `ISTERR`, `ISTNRM` | por quadro |
| `8C17BFB4`, `8C17C208`, `8C17C2D0`, `8C1C5368`, `8C1C539C` | Shinobi: máscaras e estado do INTC do Holly | por quadro |

### 1.4 Geometria: quem escreve as listas [C]

A geometria **não** passa por uma chamada do Kamui por vértice. São dois transportes:

1. **Store queue direto para o TA.** As funções de desenho da Ninja programam `QACR0/1` para o TA (a sonda vê as
   escritas em `FF000038/3C` em `8C15F078`, `8C160CD4`, `8C160240` e outras) e chamam laços internos que gravam os
   parâmetros pelo `pref`. Os maiores escritores, em escritas na SQ:

   | Função | Escritas na SQ |
   |---|---|
   | `8C17EA5C` | 162 M |
   | `8C17EC50` | 64 M |
   | `8C19F61C` | 48 M |
   | `8C17E538` | 33 M |
   | `8C17FB10` | 27 M |
   | `8C18186C` | 18 M |
   | `8C19E124` | 18 M |
   | `8C1815B0` | 17 M |

   Ao todo são **cerca de 40 laços**, em `8C17E000`–`8C181FFF`, `8C199000`–`8C19FFFF`, `8C169xxx`, `8C1CDxxx`–`8C1CExxx`
   e `8C1EBxxx`.
2. **API de tiras do Kamui** (`8C187B08`, `7B58`, `7BD4`, `7C28`, `7F42`, `80BA`, `817E`): cabeçalhos de polígono e
   vértices genéricos, chamados por 11 a 14 funções da Ninja. Também usa `QACR` + SQ.
3. **Buffer de vértices + DMA** [H]: `8C18BC3C` → `8C18BB84` → `8C1C1E16`, uma vez por quadro. É preciso confirmar se o
   DMA leva listas (FIFO de polígonos, `0x10000000`) ou textura (`0x11000000`), e com quantos bytes (fase 0).

**Consequência:** interceptar a geometria **por função** exigiria reimplementar os ~40 laços da Ninja/Kamui. Isso está
fora do escopo ("não reimplemente o Kamui inteiro") e seria trabalho do N10. O ponto natural é o **fim do transporte**:
o bloco de 32 bytes que o `pref` ou o DMA entrega.

### 1.5 Estruturas envolvidas

- **Parâmetro do TA** (32 ou 64 bytes): PCW (tipo, lista, fim de tira), ISP/TSP, TCW (endereço de textura na VRAM,
  formato), vértices, volumes modificadores. Hoje é decodificado pelo `Parser` do `pvr_render`.
- **Estado do quadro**: os ~150 registradores gravados por `reg_write`. Exemplos: fundo `ISP_BACKGND_*`, `FPU_*`,
  `FOG_*` + tabela, `HALF_OFFSET`, recorte, `TA_*_BASE/LIMIT`, `FB_W_*`, `SPG_*`.
- **Polígono de fundo**: gravado pelo Kamui na VRAM, no endereço de `TA_ITP_CURRENT` (`pvr_regs.cpp` documenta isso).
- **Texturas**: bytes na VRAM no endereço do TCW (64 bits), carregados por `5A08`/`56BE`; paleta na `PALETTE_RAM`.
- **Framebuffer**: `FB_W_SOF1` (o render grava) e `FB_R_SOF1/2` (a saída de vídeo lê). A sonda não vê **leitura** da
  VRAM pelo código do jogo (nenhuma em `vram32`/`vram64`).

## 2. Fluxo atual até a IR nativa [C, código]

```
jogo/Ninja/Kamui (AOT)
  ├─ pref → sh4_sq_flush (memory/sh4_sq)  ──┐
  └─ DMA ch2 (sh4_dmac_ch2_transfer) ───────┤→ ta_fifo_write (pvr/ta) → polygonFifo: TaParam, lista, fim de lista
                                            │                                       └→ holly_intc_raise (fim de lista)
  reg_write → memória A05F8xxx → pvr_regs (efeitos: TA_LIST_INIT, STARTRENDER, SPG...)
                                   └ STARTRENDER → pvr_render (Parser::feed sobre ta_params)
                                        └ PvrCaptureSink → render_capture (transitório) → nr::CommandList
                                              └ IRenderDevice: Software (padrão) ou Vulkan
                                        └ writeBack → VRAM (FB_W_SOF1)
                                   └ fim de render: min(450000 + ta_bytes × 100, 1500000) ciclos → holly_intc (ISTNRM 0-2)
  VBlank (tempo lógico, N2) → holly_intc → ponte 8C00FA00 → 8C1982F4 → tratador do Kamui 8C1C53E2
  pvr_video lê FB_R_SOF da VRAM → apresentação
  texturas: render_capture lê os bytes da VRAM pelo TCW a cada render (cache por conteúdo)
```

## 3. Dependências por componente

| Componente | Quem usa | Contrato visível ao jogo | Medido |
|---|---|---|---|
| Registradores do PVR (`pvr_regs`) | HAL `59A4`/`59D6`; escrita direta em `4C9E`/`4914`; leituras de `810C` | `TA_ITP_CURRENT`, `TA_NEXT_OPB`, `SPG_STATUS` (fase do VBlank e linha), ID/revisão | 376 mil acessos (6 000 VBlanks) |
| FIFO do TA (`pvr/ta`) | `pref` e DMA | fim de lista → `ISTNRM` bits 7–10/21; **a contagem de bytes entra no prazo do fim de render** | 2,69 M |
| Store queues (`sh4_sq`) | ~40 laços + API de tiras | é recurso da CPU: com destino na RAM é só memória | 79 M |
| DMA canal 2 (`sh4_dmac` + `holly_sb`) | `1E16`/`1D4C`/`202C`, `5A08` | fim de DMA, `CHCR2.TE`, `SB_C2DSTAT` | 35,8 mil |
| VRAM (`vram`) | `56BE` (texturas, fundo), `512E`, `writeBack`, `pvr_video` | nenhuma leitura pelo jogo | 2,68 M |
| INTC do Holly (`holly_intc`) | tratador `53E2`, Ninja `172642`, jogo, Shinobi | `ISTNRM`/`ISTERR`/`IMLn` lidos e limpos | todo quadro |
| Saída de vídeo (`pvr_video`) | `FB_R_SOF` | troca no VBlank | todo quadro |

**Contratos de tempo que as referências fixam:**
- o prazo do fim de render, que depende de `ta_bytes()` do quadro;
- o fim de lista na hora em que o bloco chega;
- o VBlank do N2;
- no estável (`--tempo nativo`), os mesmos prazos em ciclos.

Mudar qualquer um muda o estado por passo, e as 16 referências mostram isso.

## 4. Pontos de interceptação

### 4.1 Sem regenerar nada (runtime e ponte) [C]

| Ponto | Mecanismo | Já usado |
|---|---|---|
| `pref` para a área 4 (`sh4_sq_flush`) | função do runtime | sim |
| transferência do canal 2 (`sh4_dmac_ch2_transfer`) | função do runtime | sim |
| acessos `A05F8000`–`A05F9FFF`, `A05F6800`–`A05F69FF`, área 4/VRAM | despacho de regiões do `memory.cpp` | sim (N8.4 `maple_bus`, N8.6 `sound_bus`) |
| tratador `8C1C53E2` | `sh4_override`: a chamada é **dinâmica** (pelo despachante `8C1982F4`) | sim (mods, provedores) |
| as 8 chamadas dinâmicas de `reg_write`, as de `1E16` (`8C18BB84`, `8C18D276`), `8C18817E`, `8C187F42` | `sh4_override` | — |

### 4.2 Com slot nativo (`[native] slots` no `config/bluestinger_us.toml`)

Precedente: N3–N6 e N7c. O gerador e a semântica do código traduzido **não mudam**; o `generated/` é regenerado, e só a
macro da chamada para um slot muda, conferida arquivo a arquivo. **Isso conta como "modificar o AOT"? É decisão do
mantenedor (D-K1).**

Candidatos:
- `8C1C59A4` e `8C1C59D6`;
- `8C1C5A08`, `8C1C56BE` (só as chamadas da HAL), `8C1C5AE0`;
- `8C18BC3C` (render do quadro), `8C18D276`;
- `8C172642` (laço do quadro da Ninja);
- `8C173C58` (paleta).

### 4.3 O que não se intercepta

- os ~40 laços de geometria (seria reimplementar);
- a lógica da API do Kamui (93 entradas chamadas de fora da biblioteca; o estado de listas, tiras e texturas continua
  recompilado).

## 5. Proposta: tradução direta Kamui → IR

**Princípio:** o Kamui recompilado continua decidindo **o quê** desenhar. O produto deixa de **simular o hardware** que
recebe isso. A tradução fica em serviços nativos com a semântica do Kamui, como a fachada do N8.4 (`maple_bus`) e do N8.6
(`sound_bus`): `runtime/system/graphics_bus` + `native/render/kamui`.

| Serviço novo | Recebe | Faz | Substitui no produto |
|---|---|---|---|
| **`ListSink`** (`native/render/kamui/list_sink`) | o bloco de 32 bytes do `pref`/DMA (área 4, FIFO de polígonos) | decodifica direto para a `CommandList` em construção: o `Parser` do `pvr_render` movido para o nativo, como o raster no N8.7. Conta os bytes e emite o fim de lista como evento | `pvr/ta` (FIFO, estado, `ta_params`), o `Parser` do legado, `render_capture` (transitório) |
| **`FrameState`** | `reg_write`/`reg_read` (slot), ou a região `A05F8xxx` (fachada) | guarda só os registradores que o Kamui usa e monta o `FrameParams` (fundo, fog, `FPU_SHAD_SCALE`, recorte, `HALF_OFFSET`). Comandos: `TA_LIST_INIT` reinicia o `ListSink`; `STARTRENDER` fecha a lista, entrega ao `IRenderDevice` e agenda o fim de render com **a mesma fórmula** | `pvr_regs` (o banco de hardware com efeitos) |
| **`TextureHeap`** | as cópias da HAL (`5A08`/`56BE`/caminho direto de textura) e a paleta | armazena os bytes no endereço do TCW (área de memória do guest, D-K2), marca as páginas sujas e alimenta o `TextureCache` nativo (já existe: `texture_convert`, `convert_pal8`) | `vram` como dispositivo, o `texture_source` que relê a VRAM a cada render |
| **`DisplayService`** | `4914`/`FB_R_SOF` | troca o quadro exibido no VBlank, pelo `IRenderDevice`/apresentação, sem passar pela VRAM | `pvr_video` (varredura da VRAM), `writeBack` |
| **eventos** | fim de render, fim de lista, fim de DMA | eventos do tempo lógico (N2) e do estável, os mesmos instantes; o nível IRL vai à ponte | `holly_intc` como modelo (fica um banco `ISTNRM`/`IMLn` mínimo, decisão D-K3) |
| **cópia do canal 2** | `1E16` (slot ou fachada) | cópia síncrona RAM → `ListSink`/`TextureHeap`, com o fim agendado como hoje | `sh4_dmac`, `holly_sb` (DMA) |

**Ordem de corte** (cada passo é reversível e comparável):

1. `ListSink` alimentado pelo `pref` e pelo DMA no lugar do `polygonFifo`, ainda com `pvr_regs`. O `render_capture` sai
   do produto.
2. `FrameState` no lugar de `pvr_regs`, primeiro pela região de memória (fachada) e depois, se D-K1 aprovar, pelos
   slots da HAL.
3. `TextureHeap` + `DisplayService`: a VRAM deixa de ser lida pelo render e pela apresentação.
4. Eventos e DMA: `holly_intc`, `holly_sb` e `sh4_dmac` saem do produto. Ficam o banco mínimo do D-K3 e o tratador do
   Kamui recompilado.

## 6. Preservar texturas, estado, sincronização e ordem

- **Texturas.**
  - O endereço do TCW continua a chave: o jogo e o Kamui só conhecem endereços. Elevar para identificadores é o N10.
  - O `TextureHeap` marca as páginas na cópia (o produtor é conhecido: a HAL e o caminho direto de textura) e tira a
    comparação por conteúdo a cada render. A invalidação passa a ser exata, por página.
  - **Render para textura:** FB_W_SOF1 fora da área de quadro → a `CommandList` grava num alvo do `TextureHeap` em vez
    do framebuffer. A fase 0 mede se o jogo usa isso.
- **Estado gráfico.** O `FrameState` monta o `FrameParams` **no `STARTRENDER`**, como hoje (`render_capture.cpp`
  672–686). Fog, volumes e paleta continuam iguais em 32 bits ao software (cenas de fidelidade).
- **Sincronização.**
  - O prazo do fim de render continua `min(450000 + bytes × 100, 1500000)` no relógio do modo ativo, com os mesmos
    `bytes`.
  - O fim de lista é emitido no bloco de fim de lista.
  - `TA_ITP_CURRENT`/`TA_NEXT_OPB` dão os mesmos valores. O Kamui os lê para gravar o polígono de fundo, e a aproximação
    atual também fica.
  - Nada muda no `--tempo nativo`, no padrão experimental nem no headless histórico.
- **Ordem.**
  - A `CommandList` guarda a ordem de chegada por lista (opaca, PT, translúcida, volumes), como o `Parser`.
  - O ordenamento automático continua no dispositivo (`frame_prep`).
  - O fundo é o primeiro, como hoje.

## 7. Como provar que os componentes antigos saem do produto

1. **Portão de papéis** (`tests/n8/check_papeis.cmake`):
   - `ta.cpp`, `pvr_regs.cpp`, `vram.cpp`, `pvr_video.cpp`, `holly_intc.cpp`, `holly_sb.cpp`, `sh4_dmac.cpp` e
     `render_capture.cpp` passam a objetos **só da validação**;
   - os símbolos `?ta_fifo_write@bs@@`, `?pvr_render@bs@@`, `vram_data`, `holly_intc_raise`, `sh4_dmac_ch2_transfer` e
     `PvrCaptureSink` ficam **proibidos** no mapa do produto;
   - `graphics_bus`/`list_sink`/`frame_state`/`texture_heap` ficam obrigatórios.
2. **Região sem dispositivo:** no produto, qualquer acesso fora do contrato da fachada é `sh4_fatal` com o nome do
   componente removido, como `ta_yuv_removed` (N8.5) e o barramento do N8.4. Sem recuo silencioso.
3. **Contagem zero nas rotas:** a sonda (validação) conta os acessos que caem fora da fachada, e a meta é 0 nas 16 rotas.

## 8. Plano de testes, equivalência e recuo

**Fase 0 (medição, só leitura, no exe de validação):**
- separar os bytes do DMA do canal 2 entre a FIFO de polígonos e a textura;
- `FB_W_SOF1` fora da área de quadro (render para textura);
- os registradores que o Kamui **lê** e os valores esperados;
- a sequência do tratador `53E2` (bits de `ISTNRM` tratados, encadeamento do DMA);
- a quantidade de bytes por quadro.

O resultado entra na seção 1 com [C].

**Equivalência, a cada passo da seção 5:**
- **comparação em processo**: o modo `comparar` do N7 vira "`ListSink` × `Parser` legado" na validação. As duas
  `CommandList` são iguais por render (hash dos desenhos, vértices e estado) em todos os renders das rotas de combate,
  NEW GAME e load;
- **16 referências do N8.0** com `BS_RENDER_SEM_FOG_VOLUMES=1`: 16/16, com as 179 exceções e nenhuma nova;
- **`referencias_n87`**: 16/16 sem exceção (quadros com os efeitos);
- **Vulkan × software** em 32 bits nas cenas de fidelidade; AMD: `n87_amd.ps1` iso/par16/seq, N = 30.

**Negativos** (cada um tem de reprovar):
- bloco do TA corrompido;
- `STARTRENDER` sem `TA_LIST_INIT`;
- página de textura não marcada (o `TextureHeap` acusa a textura velha);
- acesso a um registrador fora do contrato (`sh4_fatal`);
- prazo do fim de render alterado (`BS_*_MUTACAO`: as referências reprovam);
- render para textura sem suporte (erro explícito, não omissão).

**Recuo e modelos:**
- os modelos de hoje (`ta`, `pvr_regs`, `vram`, `holly_*`, `sh4_dmac`, `pvr_render_ta`, `render_capture`) ficam
  **inteiros** no build de validação, atrás de `--graficos legado`. É o mesmo padrão de `--audio`/`--video`/`--controles
  legado` no N8.4–N8.6. No produto, a opção é recusada (saída 2);
- recuo de release: a Preview anterior fica em `dist` (`.n8-5-ced7570c` etc.);
- cada passo da seção 5 é um commit próprio, com a catraca do mapa atualizada no mesmo commit.

**Revisão:** Codex depois de cada passo da seção 5, antes da bateria longa.

## 9. Decisões do mantenedor

- **D-K1 (mecanismo):**
  - (a) só o runtime e a ponte (seção 4.1): sem regenerar; o `FrameState` é uma fachada de memória como o N8.4/N8.6;
  - (b) (a) + slots da HAL (seção 4.2): os registradores entram como chamadas com semântica e a região MMIO fica sem
    contrato no produto, que é a prova mais forte; exige regenerar o `generated/` (só macros).

  Recomendo **(a) primeiro**, com (b) como segundo corte se o mantenedor considerar os slots compatíveis com "não
  modificar o AOT".
- **D-K2 (VRAM):** o `TextureHeap` endereçado como a VRAM é memória do guest, como a RAM, não hardware simulado.
  Recomendo aceitar, como o diagnóstico já recomendava.
- **D-K3 (estado de interrupção):** o tratador do Kamui e a Ninja leem e limpam `ISTNRM`/`IMLn`.
  - (a) Recomendo manter um banco mínimo de status com as fontes nativas, como a ponte do N8.1 fez com o INTC.
  - (b) A alternativa é substituir o tratador `53E2` por um nativo, o que é reimplementar parte do Kamui.
- **D-K4 (estável):** o `--tempo nativo` usa os mesmos prazos em ciclos, e a semântica dele não pode mudar. Proponho que
  os serviços sejam a fonte nos dois modos. Os modelos que só o estável usa (o INTC do SH-4 fora da ponte) ficam fora
  deste corte, sem mudança.

**Fora do escopo:** reimplementar a API Kamui/Ninja, elevar texturas para identificadores (N10), N8.8 completo, N9, N10.

## 10. Decisões aprovadas (mantenedor, 2026-10-28)

- **D-K1 = (a):** interceptação só no runtime, sem regenerar o AOT nem mudar o gerador. Nesta etapa não há slots nativos
  da camada do Kamui.
- **D-K2 = `TextureHeap`:** preserva os endereços do TCW e todos os efeitos de memória que o código recompilado observa:
  escritas parciais, invalidação, DMA e aliases.
- **D-K3 = (a):** um banco mínimo de status alimentado por eventos nativos. O tratador de interrupções original do Kamui
  continua, com a mesma semântica de leitura, limpeza e prioridade.
- **D-K4 condicionada:** os serviços nativos só fornecem os eventos nos dois modos se o `--tempo nativo` preservar
  inteira a sua semântica e as referências históricas. Relógios, ordem das interrupções e prazos não mudam para
  simplificar a arquitetura.

**Ordem obrigatória:**
1. medição da fase 0;
2. `ListSink`, com a IR validada render a render;
3. `FrameState`, `TextureHeap` e `DisplayService`, em incrementos independentes;
4. substituição progressiva das fronteiras TA, PVR e VRAM, com os contratos de eventos preservados;
5. remoção dos objetos legados do produto só depois da independência funcional demonstrada e do mapa do linker conferido.

**Regras:**
- `holly_*` e `sh4_dmac` só saem inteiros depois de provado que as responsabilidades não gráficas deles (Maple, G2,
  GD-ROM, INTC) foram preservadas;
- equivalência de estados, eventos, IRQs, DMA, contadores, temporização e IR;
- testes negativos para os acessos fora do contrato;
- nenhum comparador enfraquecido e nenhuma exceção nova automática;
- as referências do N8.0 que comparam quadros históricos usam o modo sem fog e volumes; as do N8.7 validam os efeitos;
- divergência funcional, ou necessidade de regenerar o AOT, interrompe o incremento e vai ao mantenedor com a evidência.

## 11. Fase 0: medição (2026-10-28)

Sonda `BS_HW_PROBE_GRAFICOS=<arquivo>` (`runtime/debug/gfx_probe`), só na validação. No produto a variável é recusada
(saída 2), e o portão confere o objeto. A sonda não muda o comportamento: com e sem ela, as mesmas linhas de progresso e
`rc=0`.

Rota `passo_newgame_area1`, 40 000 VBlanks, 19 984 renders:

| Pergunta | Resultado |
|---|---|
| DMA do canal 2: listas ou texturas? | **as duas.** Polígonos: 39 968 transferências, 788 MB (64 B a 358 KB, o buffer de vértices do Kamui). Textura: 124 transferências, 1,3 MB (`8C1C5A08` para `0x11xxxxxx`). |
| Bytes de polígono por render | store queue direto: média 67 KB (max 453 KB); DMA: média 39 KB (max 358 KB); total: média 107 KB (max 630 KB) |
| Escritores das listas por SQ | 33 funções; as maiores sao `8C17EA5C` (28,9 M blocos), `8C17E538` (4,5 M), `8C1815B0` (2,1 M), `8C19E124` (1,3 M) e `8C19A234` (1,2 M) |
| Render para textura | **não há.** `FB_W_SOF1` só vale `0x200000`/`0x600000` (o buffer duplo), os dois exibidos (`FB_R_SOF1/2`), nunca o bit 24; nenhuma das 259 926 texturas amostradas começa num alvo de render |
| VRAM pelo caminho de memória | `8C1C56BE` (cópia: 2,1 M escritas pelo caminho de 32 bits, 0,9 M pelo de 64) e `8C1C512E` (959 mil, caminho de 32 bits, chamada pelo tratador). O jogo nunca lê a VRAM |
| Registradores que o Kamui lê | `ISTNRM`/`ISTERR`, `SPG_STATUS` (`810C`), `TA_ITP_CURRENT` (`8138`, no fim da lista translúcida), `CHCR2` (`FFA0002C`), `FB_R_CTRL`/`VO_CONTROL` (uma vez). Pela HAL: `ID`, `REVISION`, `TEXT_CONTROL` e `TA_LIST_INIT` |

Rota `passo_combate` (54 344 VBlanks, 27 149 renders, com fog e volumes), as mesmas conclusões:
- DMA de polígonos: 54 298 transferências, 1,6 GB; de textura: 661, 5,8 MB;
- bytes por render: SQ 111 KB em média (máximo 745 KB), DMA 60 KB (357 KB), total 171 KB (949 KB);
- render para textura: nenhum. Só os dois framebuffers exibidos, e nenhuma das 713 519 texturas amostradas começa num
  alvo de render;
- tratador: 163 584 chamadas, as mesmas 24 sequências.

**Sequência do tratador `8C1C53E2`** (120 043 chamadas, 24 sequências distintas). Por render:
1. `ISTNRM = 0x80110`: fim do DMA do canal 2 + fim da lista do modificador opaco. Le `CHCR2`, limpa o `TE` e limpa os
   bits 19 e 8;
2. `0x90`: fim da lista opaca (bit 7);
3. `0x80210`: fim do DMA + fim da lista translúcida. Limpa e lê `TA_ITP_CURRENT` duas vezes;
4. `0x17`: fim de render (bits 0 a 2).

No VBlank-In (`0x18`, uma vez por campo), o tratador:
- grava `FB_R_SOF1/2` (troca do quadro exibido);
- grava os registradores do próximo render e o `STARTRENDER` (o render começa **dentro** do VBlank);
- **dispara o DMA do Maple** (`5F6C04`/`5F6C18`) e lê o estado do G2 (`5F7818`) e o `SB_FFST` (`5F688C`).

Essas responsabilidades não gráficas (Maple, G2, FIFO do sistema) ficam com `holly_sb`/`maple_bus` e não saem com o
corte gráfico.

**Consequências para o desenho:**
- o `ListSink` recebe os dois transportes (SQ e DMA); o DMA de textura vai ao `TextureHeap`;
- sem render para textura, o `DisplayService` só precisa do buffer duplo; um `FB_W_SOF1` fora dele vira erro explícito;
- o banco mínimo do D-K3 tem de responder `ISTNRM`/`ISTERR`/`IMLn`, `CHCR2.TE`, `SPG_STATUS` e `TA_ITP_CURRENT` com
  os valores de hoje.

## 12. Plano dos incrementos 3 e 4 (depois da fase 0)

**Cobertura do contrato:**
- a fase 0 mediu 3 rotas;
- um contrato estrito, só com os registradores vistos, poderia derrubar o produto num caminho do jogo que as rotas não
  cobrem (outras áreas, menus raros, game over).

Por isso o `FrameState`:
- **aceita todo o mapa de registradores que o Kamui usa** (dados simples, comandos, temporização, tabela de fog,
  paleta), com a semântica de hoje;
- **recusa com erro explícito só o que a IR desenharia errado**: render para textura, ou seja, `FB_W_SOF1` fora do
  buffer duplo exibido; o YUV, já recusado no N8.5; e estados que o decodificador não representa (já fatais).

O que ainda não for medido continua aceito com a semântica atual e coberto pela comparação na validação.

**Padrão de cada incremento** (o mesmo do `ListSink`):
1. o serviço nativo fica no produto;
2. na validação, o modelo do legado continua a referência e roda junto;
3. um comparador confere cada acesso ou render (valores lidos, prazos, eventos), com um negativo por mutação;
4. a catraca do mapa proíbe o modelo no produto;
5. referências N8.0 (com o diagnóstico sem fog e volumes) e N8.7, Codex e commit próprio.

**Temporização do vídeo (SPG):**
- as funções de posição do feixe, cruzamento de linha e próxima troca de linha alimentam os provedores de tempo nos dois
  modos;
- elas saem do banco do PVR para um módulo compartilhado, sem mudança de conta;
- a comparação do `--tempo nativo` com as referências históricas é o critério (D-K4).

## 13. Incremento 3a: FrameState (2026-10-28)

- **Nativo:** `native/render/kamui/frame_state` guarda os registradores gráficos, responde ID, REVISION e SPG_STATUS,
  executa os comandos que não guardam o valor e atualiza os ponteiros do TA no `TA_LIST_INIT`. Também converte a paleta e
  faz as contas do SPG. Os efeitos vão para uma interface (`FrameEffects`), na ordem do modelo anterior. O custo do fim
  de render é a função pura `render_done_cost`.
- **Produto:** `runtime/system/graphics_bus` serve a API de `pvr/pvr_regs.h` sobre o FrameState e executa os efeitos:
  render, prazo, sincronização do INTC, listas, temporização e YUV recusado. O `time_now` é lido nos mesmos pontos de
  antes. O `pvr_regs.cpp` sai do produto.
- **Validação:** o banco do legado continua ativo. O FrameState roda em sombra e é comparado a cada acesso:
  - valores lidos;
  - registrador gravado e ponteiros do TA;
  - sequência de efeitos de cada escrita;
  - prazo do fim de render, contra a fórmula do produto;
  - banco e paleta a cada render;
  - contas do SPG.
  
  Negativos `BS_FRAME_STATE_MUTACAO=leitura|escrita|fase|efeitos|prazo`; resumo `[frame-state]`.
- **Equivalência:**
  - combate (estável): 2 605 652 comparações, 0 diferentes;
  - combate (experimental): 2 041 730, 0 diferentes;
  - NEW GAME: 2 067 891, 0 diferentes;
  - quadros e eventos do produto iguais aos do incremento 2.

## 14. Incremento 3b: TextureHeap (2026-10-29)

- **Nativo:** `native/render/kamui/texture_heap` é a memória (D-K2): 8 MB no layout do caminho de 32 bits, `offset64`
  para a vista de 64 bits, escritas parciais, a volta no fim dos 8 MB e uma geração por página de 4 KB.
  `generation64(off64, n)` dá a maior geração das páginas que um intervalo do caminho de 64 bits ocupa nos dois bancos.
- **Produto:** `runtime/system/texture_heap_bus` serve `pvr/vram.h` (com `vram_touch` e `vram_generation64`). O
  `vram.cpp` sai do produto.
- **Escritores diretos** (marcam com `vram_touch`):
  - o caminho direto de textura do TA, com 16 bytes em cada banco por bloco;
  - a gravação do render, por linha;
  - o conversor YUV (validação).
- **Cache de texturas:** o envio é reaproveitado quando as gerações do intervalo (`texture_span`, o mesmo de
  `texture_source`) e da paleta (formatos 5 e 6; `palette_generation` do FrameState) não mudaram. As gerações só são
  confirmadas depois do envio que deu certo.
- **Validação:** o `vram.cpp` continua ativo e a memória nativa roda em sombra:
  - as páginas tocadas são comparadas a cada render (`[texture-heap]`);
  - o cache relê os bytes e conta as texturas velhas (`[texture-cache]`);
  - negativos `BS_TEXTURE_HEAP_MUTACAO=marca` e `BS_TEXTURE_CACHE_MUTACAO=antecipa`.
- **Equivalência:** combate (4 106 438 páginas) e NEW GAME (3 020 507), 0 diferentes e 0 texturas velhas; referências
  com o produto (`3f7e74e08f6d055f…`): N8.0 sem fog e volumes 16/16 (só as 179 exceções), N8.7 16/16.

## 15. Incremento 3c: DisplayService (2026-10-29)

- **Nativo:** `native/render/kamui/display_service` gera a imagem exibida (`scanout`) a partir dos seis registradores
  de leitura do framebuffer (`DisplayRegs`: FB_R_CTRL, FB_R_SOF1/2, FB_R_SIZE, SPG_CONTROL e VO_CONTROL) e da memória
  das texturas. A conta é a mesma da saída anterior: formatos 0555, 565, 888 e 0888, duplicação de linha, os dois campos
  intercalados com o módulo, preto com a leitura desligada ou em blank, e cada byte com a volta no fim dos 8 MB.
- **Produto:** `runtime/system/display_bus` serve `pvr/pvr_video.h` sobre o FrameState e o TextureHeap, junto com o
  gancho do VBlank (chamado pelo `holly_intc` nos mesmos pontos). O `pvr_video.cpp` sai do produto (portão).
- **Validação:** o `pvr_video.cpp` continua sendo a referência. A cada quadro, o serviço nativo roda em sombra sobre o
  FrameState em sombra (`pvr_shadow_reg`) e a memória em sombra (`vram_shadow_data`), e o quadro inteiro é comparado
  (`[display]`). O negativo `BS_DISPLAY_MUTACAO=campo` troca os dois campos na sombra. O ctest `n87_display_mutacao`
  exige que o caso de sombra falhe com ele.
- **Equivalência:**
  - combate: 54 344 quadros, 0 diferentes;
  - NEW GAME: 40 000 quadros, 0 diferentes;
  - com `campo`, 26 079 quadros acusados no combate (o primeiro no quadro 54);
  - os outros comparadores continuam em 0: ListSink, FrameState, TextureHeap e texturas velhas;
  - referências com o produto (`e85706089cc447fa…`): N8.0 sem fog e volumes 16/16 (só as 179 exceções), N8.7 16/16.

## 16. Incremento 4a: banco de status de interrupção (D-K3, 2026-10-29)

Divisão do passo 4 da seção 5 (eventos e DMA), cada parte com o padrão da seção 12 e commit próprio:
- **4a:** o banco de status do Holly;
- **4b:** a cópia do canal 2, ou seja, o `sh4_dmac` e a parte do `holly_sb` que dispara o DMA. O Maple, o G2, o
  `SB_FFST` e as faixas removidas ficam no `holly_sb`;
- **4c:** a entrada da área 4 (`ta.cpp`);
- **4d:** a orquestração do render (`pvr_render` e `render_capture`).

- **Nativo:** `native/render/kamui/event_status` guarda:
  - o status (`ISTNRM`/`ISTEXT`/`ISTERR`, escrever 1 limpa) e as máscaras (`IML2/4/6` NRM, EXT e ERR);
  - os eventos vencidos (`deliver`, na ordem do controlador anterior) e os levantados (`raise`);
  - o nível IRL (6, 4, 2) e as contagens de VBlank, de VBlanks fundidos e de eventos. A contagem de eventos é do
    processo e não zera no reset.
- **Produto:** `runtime/system/event_bus` serve `pvr/holly_intc.h` sobre o banco, com a mesma cola de antes:
  - os eventos vêm do provedor de tempo, nos mesmos pontos (D-K4: nenhum relógio, ordem ou prazo muda);
  - os traces, o consumo dos prazos de render e do Maple, o `sh4_irq_wakeup` e o gancho do VBlank, que roda depois do
    estado.

  O tratador original do Kamui (`8C1C53E2`) lê e limpa o banco como antes. O `holly_intc.cpp` sai do produto (portão).
  Os nomes da API (`holly_intc_raise`...) continuam, servidos pelo banco nativo, como `vram_data` no 3b.
- **Validação:** o `holly_intc.cpp` continua sendo a referência. O banco nativo roda em sombra, recebe os mesmos eventos
  e escritas e é comparado depois de cada operação: os 16 registradores, o nível IRL e as contagens (`[event-status]`).
  - Negativo `BS_EVENT_STATUS_MUTACAO=limpa`: a sombra ignora a limpeza do bit 19. O ctest `n87_event_status_mutacao`
    exige a falha.
  - A cola é conferida nos dois builds pelo teste `n87_event_glue_orders_bits_counts_and_the_vblank_hook` (provedor
    roteirizado, os mesmos valores esperados).
- **Equivalência:** combate com 598 802 operações e NEW GAME com 440 028, 0 diferentes. Referências com o produto
  `ece1a44b…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16. Com `limpa`, 543 675 acusadas, a primeira na operação
  170 (`ISTNRM` 110 contra 80110).

## 17. Incremento 4b: cópia do canal 2 (2026-10-29)

- **Nativo:** `native/render/kamui/channel_dma` guarda os registradores do DMAC do SH-4 (`SAR`/`DAR`/`DMATCR`/`CHCR` dos
  quatro canais e `DMAOR`) e faz o disparo inteiro de `SB_C2DST = 1`:
  - blocos de 32 bytes de `SAR2` para a área 4. O FIFO de polígonos recebe todos no mesmo endereço; o caminho de textura
    avança 32 bytes por bloco. Um comprimento que não é múltiplo de 32 copia o último bloco inteiro;
  - no fim, `SAR2` avança, `DMATCR2` zera e `CHCR2.TE` acende; `C2DSTAT` avança pelo comprimento, `C2DLEN` e `C2DST`
    zeram;
  - com `DMAOR.DME` ou `CHCR2.DE` desligado, nada muda (o `C2DST` escrito continua guardado). Um destino recusado para
    a cópia sem atualizar nada.

  A leitura da origem e o destino vêm de quem chama: o componente não toca a memória guest.
- **Produto:** `runtime/system/dma_bus` serve `memory/sh4_dmac.h` sobre a cópia nativa. A origem é lida por
  `sh4_read32`, como antes, e cada bloco vai para `ta_fifo_write`, na mesma ordem. Destino fora da área 4 é erro
  explícito (`sh4_fatal`, com o destino pedido). O `holly_sb` chama `sh4_dmac_ch2_start` e só levanta o bit 19 no banco
  de status (4a) e pede o novo poll; Maple, G2, `SB_FFST` e as faixas removidas continuam nele. O `sh4_dmac.cpp` sai do
  produto (portão).
- **Validação:** o DMAC do legado (`sh4_dmac.cpp`, com os ganchos da sonda gráfica, que só existe na validação) continua
  sendo a referência. A cópia nativa roda em sombra:
  - recebe as mesmas escritas, comparadas depois de cada uma (os 17 registradores);
  - em cada disparo copia antes do legado, lendo a origem direto da RAM guest (sem repetir os ganchos de leitura), para
    um destino que só registra os blocos. Depois do legado, a comparação confere o resultado, a sequência de blocos
    (destino e conteúdo), os registradores do DMAC e os do system block (`[canal-2]`);
  - negativo `BS_DMA_CANAL2_MUTACAO=origem` (a sombra lê a origem um bloco adiante). O ctest `n87_canal2_mutacao` exige
    a falha.
- **Equivalência:** combate com 54 959 cópias (51 048 880 blocos) e NEW GAME com 40 092 (24 659 219), 0 diferentes.
  Com `origem`, as 54 959 cópias do combate são acusadas, a primeira na operação 10. Referências com o produto
  `bc2f4af9…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.
- **Testes nos dois builds:** FIFO (`dmac_channel2_to_ta`), caminho de textura com 40 bytes até a VRAM
  (`dmac_channel2_texture_path_to_vram`) e destino fora da área 4 com o erro explícito e sem os registradores do fim
  (`dmac_channel2_outside_area4_is_an_explicit_error`). Três casos do componente em `bs_render_tests`.

## 18. Incremento 4c: entrada da área 4 (2026-10-29)

- **Nativo:** `native/render/kamui/area4_entry`, sem estado e sem tocar a memória:
  - `route`: o caminho de cada bloco de 32 bytes que as store queues e a cópia do canal 2 entregam. FIFO de polígonos
    com os bits 23-24 em 0, conversor YUV com o bit 23, caminho de textura com o bit 24; fora da área 4, recusado;
  - `list_end_bit`: o bit de `ISTNRM` do fim de cada lista (7, 8, 9, 10 e 21);
  - `texture_offsets`: o offset de cada palavra do caminho de textura na memória das texturas, 4 em cada banco.
- **Produto:** `runtime/system/area4_bus` serve `pvr/ta.h`. As listas vão para o `ListSink`, e o fim de lista acende o
  bit no banco de status (4a) no mesmo bloco. Um parâmetro inválido é erro explícito, e o caminho de textura escreve na
  `TextureHeap` (3b) e marca as duas faixas. Um bloco no conversor YUV continua sendo o erro do N8.5. O `ta.cpp` sai do
  produto (portão, com o prefixo `:ta.cpp.obj`, porque `ta.cpp.obj` também casaria `runtime_data.cpp.obj`).
- **Validação:** o `ta.cpp` fica só com a FIFO de polígonos do legado, que continua sendo a referência do `ListSink`
  (`[lista-nativa]`), e passa a ser a referência da entrada nativa, que roda em sombra (`[area-4]`). Em cada bloco ela
  confere o caminho, o bit do fim de lista e os 8 offsets do caminho de textura.
  - Negativo `BS_AREA4_MUTACAO=banco`: a sombra troca o banco da primeira palavra. O ctest `n87_area4_mutacao` exige a
    falha.
- **Equivalência:** combate com 145 051 857 blocos (179 788 de textura, 81 447 fins de lista) e NEW GAME com 66 776 246,
  0 diferentes. Com `banco`, os 179 788 blocos de textura do combate são acusados. Referências com o produto
  `185c02ac…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.
- **A composição do produto** (revisão Codex `20261029-171814`) é conferida nos dois builds:
  - as 8 palavras do caminho de textura nas duas vistas e a geração das duas faixas;
  - o parâmetro inválido como erro explícito com a palavra de controle e sem fim de lista;
  - no produto, o bloco YUV como erro explícito com o endereço físico.

## 19. Incremento 4d: passagem de render (2026-10-29)

- **Nativo:** `native/render/kamui/render_pass`, sem estado. Quem chama passa o leitor de registrador e a memória das
  texturas. A passagem dá:
  - o tamanho do render, por `TA_GLOB_TILE_CLIP`;
  - o fundo pela memória de parâmetros, com a cor do primeiro vértice e se ele tem textura;
  - os parâmetros do quadro da IR;
  - o aviso do Region Array tipo 2, quando um tile pede o modo de ordenação oposto ao global;
  - a gravação da imagem no framebuffer, nos 7 formatos de `FB_W_CTRL`, com stride, clip, a volta no fim dos 8 MB e as
    faixas marcadas.
- **Produto:** `runtime/system/render_bus` serve `pvr/pvr_render.h` com a mesma sequência do ramo do produto de antes:
  1. o fundo, antes do Region Array (a ordem dos avisos se mantém);
  2. as listas do Kamui pelo `ListSink` para a IR;
  3. o desenho pela IR (`render_capture`). O render sem desenho ou com estados que a IR não representa continua erro
     explícito;
  4. a gravação na `TextureHeap`.

  O `pvr_render.cpp` sai do produto (portão `:pvr_render.cpp.obj`).
- **Validação:** o `pvr_render.cpp` fica só com o rasterizador de referência e é a referência da passagem nativa, que
  roda em sombra em cada render (`[render-pass]`):
  - antes do render, compara o tamanho, o fundo (cor e textura), os parâmetros do quadro campo a campo (os floats pelos
    bits, porque podem ser NaN) e o Region Array;
  - depois da gravação do legado, a passagem nativa grava num armazenamento à parte. Antes disso, cada faixa que o
    legado gravou recebe o complemento do resultado, de modo que um byte que a passagem nativa deixe de escrever nunca
    passa com o valor de um render anterior (revisão Codex `20261029-205240`). A comparação confere as faixas marcadas
    e os bytes;
  - negativo `BS_RENDER_PASSO_MUTACAO=formato`: a sombra lê `FB_W_CTRL` com o bit 0 do formato trocado. O ctest
    `n87_render_pass_mutacao` exige a falha.
- **Equivalência:** combate com 27 149 renders (16 680 345 600 bytes gravados) e NEW GAME com 19 984
  (12 278 169 600), 0 diferentes. Com `formato`, 13 044 dos 27 149 renders do combate são acusados. Referências
  com o produto `a8d2f8fd…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.
- **Testes:**
  - os de formatos de framebuffer (`test_render_formats`) rodam nos dois builds e passam pela composição do produto;
  - na validação, `render_native_pass_shadow_agrees` cobre os 7 formatos com clip parcial e um segundo render no mesmo
    framebuffer com fundo texturizado;
  - cinco casos do componente em `bs_render_tests`.

## 20. Passo 5a: o renderizador sobre a IR separado do adaptador do legado (2026-10-30)

- **Antes:** a classe `Capture` de `transitional/render_capture.cpp` era, ao mesmo tempo, o renderizador real do produto
  (Vulkan, software sobre a IR, cache das texturas, resolução interna, estatísticas) e o receptor dos retornos do
  decodificador do legado (`PvrCaptureSink`). O produto a ligava só pela entrada `render_list`, mas o objeto e o
  `PvrCaptureSink` entravam no mapa.
- **Agora são duas partes:**
  - `transitional/render_core` (os dois builds): o `Renderer`, com o dispositivo, as texturas, a resolução interna, as
    estatísticas, o resumo e a API de `render_capture.h`, que não muda. Ele implementa `PvrListRenderer`, a interface nova
    de `pvr/pvr_render.h` pela qual o produto (`system/render_bus`) entrega a IR do `ListSink`;
  - `transitional/render_capture.cpp` (só a validação): o adaptador do `PvrCaptureSink`. Ele monta a IR no `Renderer`
    pelos retornos do decodificador, chama o desenho dele e faz as conferências do modo conferir, a do `ListSink`
    contra a captura e a comparação do Vulkan com o software;
  - o `PvrCaptureSink`, o `pvr_render_set_capture` e o resto do bloco do legado em `pvr_render.h` ficam sob
    `BS_VALIDACAO`. `install()` liga o renderizador pela interface do build: `pvr_render_set_renderer` no produto e o
    adaptador na validação.
- **Portão:** `:render_capture.cpp.obj` e o símbolo `PvrCaptureSink` proibidos no produto; `:render_core.cpp.obj` e
  `?draw@Renderer@render_capture@transitional@bs@@` obrigatórios nos dois builds.
- **Provas:**
  - unitários: produto 329/0, validação 348/0; render 70/0; portão 35/47; negativos `n87_*` passam;
  - rotas no exe de validação (todas as sombras em 0 diferenças): combate 27.149 renders (`[lista-nativa]`,
    `[render-pass]` 16.680.345.600 bytes, `[canal-2]`, `[area-4]`, `[event-status]`), NEW GAME 19.984 renders; com
    `BS_LISTA_NATIVA_MUTACAO=vertice` o `[lista-nativa]` acusa 13.145 dos 27.149 renders;
  - referências com o exe do produto `219bb52c…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.


## 21. Passo 5b: o system block nativo (2026-10-30)

- **Responsabilidades não gráficas preservadas** (a regra do mantenedor: `holly_sb` só sai com elas provadas):
  - **Maple** (DMA em `0x005F6C04`/`0x005F6C18` e o resto do bloco do Maple): continua no barramento sem dispositivos do
    N8.4 (`system/maple_bus`), consultado antes do bloco, como antes;
  - **banco de status** (`0x005F6900-0x005F693F`): o `EventStatus` do 4a, consultado antes, como antes;
  - **SB_FFST** (`0x005F688C`) e a configuração dos DMAs: o último valor escrito, com o registro da primeira leitura;
  - **guarda do G2** (`0x005F7818/38/58/78`): a partida com o bit 0 é erro explícito no produto (N8.6);
  - **drive de GD-ROM e DMA do G1**: componentes removidos (N8.3), salvo `GDST = 0` da tabela do SDK;
  - **partida do canal 2** (`SB_C2DST`): a cópia do 4b e o bit 19 de `ISTNRM` no banco de status;
  - os negativos do N2b-2 (`BS_SB_MUTACAO=c2dst_ocupado|ffst_ocupado`) e os diagnósticos `BS_PVR_DEBUG`/`BS_SB_LOG`
    continuam no produto, com o mesmo efeito.
- **Nativo:** `native/render/kamui/system_block` guarda os registradores e decide o que cada acesso pede: revisão,
  componente removido, último valor com a marca da primeira leitura, partida do canal 2 ou do G2.
- **Produto:** `runtime/system/system_block_bus` serve `pvr/holly_sb.h` na mesma ordem de antes (componente removido,
  faixa, Maple, mutações do N2b-2, o bloco) e com as mesmas mensagens. O `holly_sb.cpp` sai do produto (portão
  `:holly_sb.cpp.obj`; `system_block_bus` só no produto; `:system_block.cpp.obj` e `SystemBlock::write` obrigatórios).
- **Validação:** o `holly_sb.cpp` é a referência e o bloco nativo roda em sombra em cada acesso (`[system-block]`): a
  decisão de componente removido, a faixa, o valor lido e a marca da primeira leitura, o valor guardado e as partidas do
  canal 2 e do G2. A do G2 é comparada e contada, mas não executada, porque a validação tem o modelo da AICA. O que a
  cópia faz nos registradores do canal 2 é do 4b (sombra `[canal-2]`), e a sombra recebe o mesmo resultado.
  - Negativo `BS_SYSTEM_BLOCK_MUTACAO=valor`: a sombra guarda o valor com o bit 0 trocado. O ctest
    `n87_system_block_mutacao` exige a falha;
  - negativo `BS_SYSTEM_BLOCK_MUTACAO=g2`: a sombra recebe as partidas do G2 com o bit 0 apagado. O ctest
    `n87_system_block_g2` exige que a primeira diferença seja a partida do G2 em `0x005F7818`.
- **Testes:** quatro casos do componente em `bs_render_tests`; `sb_native_system_block_shadow_agrees` na validação. A
  composição continua coberta nos dois builds por `test_port` (revisão), `test_n83_sistema` (componentes removidos),
  `test_n86_som` (G2), `test_m4_hw` (canal 2) e os testes do Maple e do N8.4.
- **Revisão Codex `20261030-054124`:** [MÉDIA] a sombra só contava as partidas do G2, sem comparar a decisão com a
  do legado: **aceito**. A sombra compara e o teste pede as quatro partidas, com o negativo `n87_system_block_g2`.
- **Provas:**
  - unitários: produto 329/0, validação 349/0; render 74/0; portão 35/47; os negativos `n87_*` passam (7, com os dois
    do system block);
  - rotas no exe de validação, com todas as sombras em 0 diferenças: combate `[system-block]` 1.880.750 acessos
    (1.715.801 leituras, 164.949 escritas, 54.959 partidas do canal 2, nenhuma do G2); NEW GAME 1.448.321 acessos
    (40.092 partidas do canal 2). As sombras do 3a ao 4d continuam em 0. NEW GAME com `BS_SYSTEM_BLOCK_MUTACAO=valor`:
    1.073.290 diferentes, a primeira no valor guardado em `0x005F6800`;
  - referências com o exe do produto `57cf9344…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.

## 22. Passo 5c: os nomes do legado fora do produto (2026-10-30)

- **Antes:** os serviços nativos do produto atendiam o caminho gráfico com os nomes dos modelos do legado
  (`ta_fifo_write`, `pvr_render`, `vram_data`, `holly_intc_raise`, `sh4_dmac_ch2_start`), declarados nos cabeçalhos do
  legado. Um objeto do legado que voltasse ao produto não seria visível por esses nomes.
- **Agora:**
  - `runtime/system/graphics_ports.h` tem as entradas neutras que o código dos dois builds chama: `area4_write`,
    `render_pass_run`, `texture_memory`, `event_status_raise` e `channel2_start`. No produto, quem as define são os
    serviços nativos (`area4_bus`, `render_bus`, `texture_heap_bus`, `event_bus`, `dma_bus`). Na validação, elas repassam
    em linha aos modelos do legado, que mantêm os nomes antigos;
  - as declarações antigas ficam sob `BS_VALIDACAO` em `pvr/ta.h`, `pvr/pvr_render.h`, `pvr/vram.h`,
    `pvr/holly_intc.h` e `memory/sh4_dmac.h`. O produto que chamasse um nome antigo não compilaria;
  - chamadores trocados: as store queues (`sh4_sq.cpp`), as texturas (`pvr_texture.cpp`), o Maple (`maple.cpp` e
    `system/maple_bus.cpp`), os serviços do produto, o `bs-diff` e os testes.
- **Nome corrigido:** o plano (seção 7) citava `sh4_dmac_ch2_transfer`, que não existe. A entrada do canal 2 do legado
  é `sh4_dmac_ch2_start`, e é ela que o portão proíbe.
- **Portão:**
  - os cinco nomes antigos (`?ta_fifo_write@bs@@`, `?pvr_render@bs@@`, `?vram_data@bs@@`, `?holly_intc_raise@bs@@`,
    `?sh4_dmac_ch2_start@bs@@`) são proibidos no mapa do produto;
  - as cinco entradas novas são obrigatórias nele. Na validação não se exige a ausência, porque o build de depuração
    pode emitir a cópia do repasse em linha;
  - a prova fora da linha: um mapa do produto com `?vram_data@bs@@` ou `?sh4_dmac_ch2_start@bs@@` acrescentado é
    recusado ("nome do legado no produto"), e um sem `?texture_memory@bs@@` também ("ausente do produto").
- **Revisão Codex `20261030-090455`:** nenhum achado.
- **Provas:**
  - unitários: produto 329/0, validação 349/0; render 74/0; portão 35/47; os 7 negativos `n87_*` passam;
  - rotas no exe de validação, com todas as sombras do 3a ao 5b em 0 diferenças e as mesmas contagens do 5b: combate
    27.149 renders (`[system-block]` 1.880.750 acessos), NEW GAME 19.984 renders (1.448.321 acessos);
  - referências com o exe do produto `2d393379…`: N8.0 16/16 (179 exceções usadas) e N8.7 16/16.
