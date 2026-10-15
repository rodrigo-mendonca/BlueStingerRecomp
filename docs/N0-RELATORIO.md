# N0: relatório da fundação do port nativo

Data: 2026-10-15. Diretriz: `docs/PLANO-PORT-NATIVO.md` v2, aprovado com autorização
**somente para o N0**.

O N0 não muda o comportamento do jogo. Prova disso:
- os testes existentes passam;
- o quadro final é idêntico bit a bit ao do build anterior ao N0 em quatro pontos do roteiro
  NEW GAME (seção 13);
- o trace de ordem é idêntico entre execuções;
- a instrumentação nova fica desligada por padrão.

Nada de N1-N9 foi implementado. Este relatório é o pedido de aprovação para começar o N1.

## 1. Resumo

| Pergunta | Resposta (com evidência) |
|---|---|
| O jogo acessa o hardware diretamente? | **Não**, nem na análise estática nem na execução. Estática: 0 de 625 referências a hardware em funções do jogo (`hw.refs.camada.jogo = 0`). Dinâmica: 0 de 1 592 acessos distintos no roteiro NEW GAME (8 000 VBlanks, `sonda.acessos.camada.jogo` ausente). |
| Por onde o jogo chega ao Dreamcast? | Por **267 funções do SDK**: 250 chamadas diretas e 17 por ponteiro (45 observadas pela sonda). Essa é a fronteira declarada em `config/fronteiras_us.toml`. |
| Existe caminho que contorna essa fronteira? | **Nenhum provado, mas o fechamento ainda não é completo.** 0 violações nos caminhos que a análise resolve (chamadas diretas, endereços tomados pelo jogo, arestas observadas em execução). O caso negativo (fronteira sem uma entrada) falha como deve. Ficam **1 443 alvos indiretos possíveis não resolvidos**: funções do SDK com endereço só em tabelas de dado, de dono desconhecido. Elas são contadas e estão na catraca (seção 5). |
| Quem acessa o hardware? | 197 funções: SDK (Shinobi, Kamui, Ninja, CRI, `sd`), tratadores de interrupção e rotinas realocadas. 159 delas ficam atrás de entradas da fronteira; 38 só são alcançadas pelo runtime (interrupção, início de thread). |
| As interfaces nativas dependem do Dreamcast? | Não. `native/` compila sozinho e o teste `n0_native_limpo` proíbe `Sh4Context`, `sh4_`, `dcmap` e endereços do guest. |

## 2. Arquitetura atual (ponto de partida)

```
1ST_READ.BIN --bs-recomp--> generated/ (C++ sobre Sh4Context, ~993 mil linhas)
                                 |
BlueStinger.exe = app/ (SDL3) + bs_generated + bs_runtime (legado)
  bs_runtime: CPU SH-4 (contexto, ops, fibras), memoria (RAM 16 MB + caminho lento),
              PowerVR2/TA/VRAM (rasterizador por software), AICA (sem ARM7), Maple,
              GD-ROM HLE sobre data\ (RuntimeDisc), BIOS HLE, perifericos do SH-4
```

O executável:
- tem 4 083 funções, das quais 1 365 do jogo e 2 635 do SDK (seção 4);
- foi ligado como **partida do Shinobi → objetos do jogo → bibliotecas**, em três blocos de
  código (seção 4.2).

Medidas no código gerado (`bs-hwscan --gerado generated`), que **reproduzem a seção 1.1 do
plano**:

| Métrica | Valor |
|---|---|
| Arquivos / linhas | 12 / 993 369 (o plano dizia ~977-981 mil; o gerador ganhou `BSG_FN`/slots desde então) |
| `BS_FN_ENTER` (funções) | 4 083 |
| `BS_CALL` / `BS_CALL_REENTRANT` | 14 987 / 14 |
| `BS_POLL` | 2 287 |
| `sh4_dispatch` (despacho indireto) | 2 601 |
| Operações de FPU (`bs::op::F*`) | 71 999 |

## 3. Ferramentas e módulos do N0

| Item | Onde | O que faz |
|---|---|---|
| Mapa de regiões | `tools/dcmap/` (`bs_dcmap`) | Classifica um endereço SH-4 por região (Holly, PVR, TA, VRAM, AICA, G1/G2, Maple, GD-ROM, P4, store queues, vetores de syscall do BIOS...) e pelo subsistema que a substitui (N1-N7). |
| Varredura estática | `translator/hwscan/` (`bs-hwscan`) | Propagação de constantes por função; acessos com endereço resolvido; literais com forma de endereço (filtrando máscaras, floats e cores); palavras de dado que são registradores; `pref` sem endereço; grafo de chamadas; strings e dados referenciados. |
| Identificação do SDK | `hwscan/sdkid.*` | Camada (jogo/SDK) e família (Shinobi, Kamui, Ninja, CRI, `sd`, BIOS, runtime C), com evidências e confiança (4.1). |
| Fronteira e fechamento | `hwscan/closure.*`, `boundary_io.*` | Deriva a fronteira, confere o fechamento contra a declarada e calcula a dominância (5). |
| Métricas e catraca | `hwscan/metrics.*` | `metricas.toml`. As chaves com catraca não podem piorar em relação a `config/metricas_base_us.toml`. |
| Sonda dinâmica | `runtime/debug/hw_probe.*` | `BS_HW_PROBE=<tsv>`: acessos fora da RAM, store queues e desvios indiretos, por função do guest (`c->cur_fn`). |
| Trace de ordem | `runtime/debug/event_trace.*` | `BS_EVENT_TRACE=<tsv>`: VBlank, interrupções, continuações por `rte`, threads, longjmp. |
| Interfaces nativas | `native/` (`bs_native_api`) | Seção 6. |
| Interoperabilidade | `transitional/` (`bs_transitional`) | Seção 7. |
| Legado nomeado | `legacy/` (`bs_legacy`) | Alvo que nomeia o runtime atual (os arquivos não mudaram de lugar). |
| CDI fora do produto | `bs_runtime_cdi` | `image_source.cpp` e `boot/disc_boot.cpp` saíram do `bs_runtime`. O teste `n0_produto_sem_cdi` confere o mapa do linker do `bluestinger.exe`. |

Para reproduzir os resultados:

```
bs-extract "ISO/Blue Stinger.cdi" game-data
bs-hwscan config/bluestinger_us.toml game-data/1ST_READ.BIN analysis/hwscan \
          --fronteira config/fronteiras_us.toml --metricas-base config/metricas_base_us.toml \
          [--gerado generated] [--sonda <tsv>]
$env:BS_HW_PROBE="sonda.tsv"; $env:BS_INPUT_SCRIPT="tools/dist/roteiros/newgame_area1.txt"
bluestinger.exe --data <data> --headless 8000
powershell -File tools/dist/test_ordem.ps1
```

As saídas (em `analysis/`, não versionado) são:
- `hw_manifest.tsv`;
- `hw_dominancia.tsv`;
- `sdk_funcs.tsv`, `sdk_faixas.tsv`, `sdk_modulos.tsv`;
- `blocos.tsv`, `callgraph.tsv`, `strings.tsv`;
- `pref_sq.tsv`, `fronteira.tsv`, `fechamento.tsv`, `metricas.toml`.

## 4. Dependências identificadas

### 4.1 Funções por camada e família

O executável não tem símbolos. Cada função recebe votos de evidências independentes:

| Evidência | Peso | Exemplo |
|---|---|---|
| **nome** (dono de uma mensagem) | 4 | `"E1225:sdMemBlkCreate return error in mwSndOpenPort"` → CRI, `mwSndOpenPort`; `"FT_LoadRoomData (): ..."` → jogo |
| **assinatura** de código | 4 | laço `div0s/div1` (divisão do runtime C), constante `0x41C64E6D` (`rand`), `extu.w r4` + `fsca` (ângulo de 16 bits do Ninja) |
| vetor do **BIOS** | 4 | lê 0x8C0000B0-BC (stub de syscall) |
| **módulo** | 2 | referência a dados do bloco de um módulo; as strings de cada módulo vêm antes do banner `"xxx Ver ... Build:"` |
| **faixa** | 2 | entre duas sementes da mesma família, a no máximo 64 funções |
| **hardware** | 2 | dispositivo de dono único (PVR/TA → Kamui, AICA → `sd`, TMU/Maple/GD-ROM → Shinobi) |
| **padrão** | 2 | `tst r4,r4` + `ftrv`/`fsca` (matriz do Ninja) |
| **grafo** | 1 | todos os chamadores são de uma família |

A **confiança** depende de quantas evidências concordam:
- alta: nome, assinatura ou BIOS sem conflito, ou duas evidências estruturais;
- média: uma evidência estrutural;
- baixa: só o grafo, ou conflito.

A **camada** (o que a fronteira usa) vem de três regras:
1. As bibliotecas não chamam o jogo diretamente. Logo, quem chama diretamente uma função do jogo é jogo, e tudo o que o SDK chama diretamente é SDK.
2. Partição por bloco de ligação: dentro de um bloco contíguo, uma função entre vizinhas da mesma camada fica com ela.
3. A exceção conhecida é a partida do Shinobi (`0x8C0100AC`), que chama o `main` do jogo. Ela fica como conflito.

| Camada | Funções |
|---|---|
| Jogo | 1 365 |
| SDK | 2 635 |
| Desconhecida | 82 (34 entre a partida e o jogo, 36 no bloco realocado, 12 avulsas) |
| Conflito | 1 (a partida) |

| Família | Funções | Alta | Média | Baixa |
|---|---|---|---|---|
| CRI (Sofdec, ADX, MWRNA, GSC, SJ) | 465 | 72 | 374 | 19 |
| Shinobi (sy*, gd*, pd*, bu, fm, kd) | 298 | 2 | 275 | 21 |
| `sd` (som, lado SH-4) | 82 | 3 | 79 | 0 |
| Kamui | 79 | 0 | 71 | 8 |
| Ninja | 69 | 3 | 66 | 0 |
| Jogo (por nome) | 68 | 10 | 55 | 3 |
| Runtime C | 23 | 6 | 17 | 0 |
| BIOS (stubs de syscall) | 16 | 16 | 0 | 0 |
| Sem família | 2 983 | | | |

Desses 2 983, 1 604 estão na camada SDK e o resto é jogo.

**Limite honesto:** a família fica sem identificação em 1 604 das 2 635 funções do SDK. A
camada está completa (exceto 83), e é ela que define a fronteira. A família, e depois o nome
de cada API, sai por contrato em cada etapa (N1 em diante), como a seção 3 do plano exige.
Não há nome de API sem evidência.

### 4.2 Blocos de ligação

| Bloco | Faixa | Conteúdo |
|---|---|---|
| A | 0x8C010000-0x8C010F00 | partida do Shinobi (+34 funções sem camada) |
| A | 0x8C010F2A-0x8C08B980 | **jogo** (1 365 funções) |
| A | 0x8C08BD00-0x8C09BA04 | SDK: runtime C, MWRNA/`mwSnd`, Shinobi (sy*, threads, stubs do BIOS) |
| B | 0x8C157F94-0x8C1DA220 | SDK: CRI (Sofdec/ADX/SJ), Shinobi, Ninja, Kamui, `sd` |
| C | 0x8C1EA500-0x8C1F2136 | rotinas realocadas/finais (store queues, QACR) |

Os 46 banners de módulo estão em `sdk_modulos.tsv`. 43 têm um bloco de dados limitado, usado
como evidência.

### 4.3 Hardware

| | Estático | Sonda (NEW GAME, 8 000 VBlanks) |
|---|---|---|
| Referências / acessos distintos | 625 (235 acessos, 390 constantes; 124 em palavras de dado) | 1 592 |
| No jogo | **0** | **0** |
| No SDK / sem camada / conflito (partida) | 474 / 25 / 2 | 1 503 / 89 (realocadas e tratador de interrupção em VBR+0x600) / 0 |
| Por subsistema (estático) | sistema 278, tempo 125, render 124, áudio 58, arquivos 24, controles 8, saves 8 | render 1 361, sistema 88, áudio 79, tempo 48, controles 15, arquivos 1 |

Além do que está na tabela:
- **Store queues:** 2 437 `pref` sem endereço resolvido em 325 funções (320 do SDK, 5 do bloco
  realocado, nenhuma do jogo). 288 funções carregam a base `0xE0000000` (282 do SDK, 5 sem
  camada, 1 do jogo). 260 funções têm as duas coisas e contam como **SQ provável** (render)
  no alcance de hardware da fronteira.
  - Conferência com a sonda (`analysis/hwscan/sq_provavel.tsv`):
    - as 260 são todas do SDK (249 sem família, 6 Ninja, 5 CRI), **nenhuma do jogo**;
    - 41 enviaram por SQ no roteiro NEW GAME;
    - a sonda achou **12 funções com SQ fora das 260**: montam o endereço da SQ sem o literal.
      Todas são SDK ou sem camada, e por isso a heurística estática sozinha não basta;
    - destinos observados: FIFO do TA (26,1 milhões de envios de 32 bytes) e RAM (34,0
      milhões; cópia rápida por SQ).
    - **O jogo não usou SQ** em nenhum caso: estático 0, sonda 0.
  - A função do jogo com o literal, `0x8C058720`, só guarda o valor numa
  estrutura (`mov.l r3,@(20,r13)`), sem uso de SQ (conferido no disassembly). A sonda mostra
  o envio real ao TA pelas SQ, todo a partir do SDK.
- **Literal × uso:** literais de pool colidem com máscaras (`0xE0000000`, `0xF0000000`), floats
  (`0x40000000` = 2.0f) e cores ARGB. `0xFF000000` e `0x80704820` aparecem como argumentos de
  cor em `0x8C0623C0` (disassembly). O filtro `plausible_constant` exige forma de endereço
  (P1/P2, alinhamento, registrador no início do bloco P4) e é testado.
- **Tabelas de registradores em dado:** Holly/G2/Maple/PVR-DMA em 0x8C0B8FB4 (módulo
  `syHw`), DMAC em 0x8C1C2214, TMU em 0x8C15D2C0.
- **`fmov` de memória:** o tamanho depende de FPSCR.SZ (4 ou 8 bytes), que não é propagado.
  O acesso é registrado com 8 bytes e o pós/pré-incremento invalida a base (revisão Codex).
- **Limites da varredura estática** (a sonda cobre esses casos):
  - endereços calculados em tempo de execução ou lidos de variáveis;
  - acessos relativos ao GBR;
  - no código realocado, os literais do pool vêm do arquivo, mas o jogo corrige um deles ao
    instalar a cópia (o destino de salto dos vetores, que não é endereço de hardware).

## 5. Fronteira, fechamento e dominância

**Fronteira declarada** (`config/fronteiras_us.toml`), com 267 funções:

| | Total | Arquivos | Tempo | Controles | Saves | Áudio | Render | Sistema | Sem hw |
|---|---|---|---|---|---|---|---|---|---|
| Entradas | 267 | 17 | 31 | 5 | 2 | 17 | 36 | 57 | 193 |

- Composição: 250 diretas e 17 indiretas.
- Origem: as entradas observadas na execução (45, `observada = true`) foram juntadas às da
  análise estática.
- Uma entrada conta em cada subsistema cujo hardware alcança.
- "Sem hw" são funções do SDK que não chegam a hardware: matemática do Ninja, runtime C,
  utilitários. Elas saem com a elevação (N9) ou com o provedor nativo da família.

**Fechamento:** começa nas funções do jogo, segue as chamadas diretas e as observadas sem
atravessar a fronteira e, se alcançar uma chamada indireta, abre os alvos que o jogo pode
ter em mãos. Uma violação é uma função do SDK fora da fronteira ou uma função que acessa
hardware.
- Resultado: **0 violações**, estático e com a sonda.
- **Inconclusivo para 1 443 funções** (`fechamento.indireto_nao_resolvido`, com catraca),
  achado da revisão do Codex. São funções do SDK fora da fronteira com endereço só em dado;
  se o jogo alcança uma chamada indireta, elas são alvos possíveis que a análise não prova
  nem descarta. Aparecem em `fechamento.tsv` como `indireto_nao_resolvido`. Cada etapa reduz
  esse número com a sonda em mais roteiros e com a identificação por contrato. Uma análise de
  points-to das tabelas resolveria o resto.
- Sem a sonda, a fronteira estática deixava 8 alvos indiretos do jogo de fora (matemática do
  Ninja e runtime C chamados por ponteiro). Por isso a fronteira declarada inclui os observados.

**Limites:**
- O roteiro de cobertura é um só (NEW GAME até a 1ª área). A matriz de cobertura do plano
  (seção 6) amplia isso.

**Dominância** (`hw_dominancia.tsv`): para cada função que acessa hardware, lista as
entradas da fronteira que a alcançam. Com o fechamento válido, todo caminho do jogo até ela
passa por uma delas.
- 159 funções estão atrás de entradas da fronteira.
- 38 são alcançadas só pelo runtime (interrupção, início de thread). Esse é o grupo que o N2
  (eventos nativos) tem de cobrir.

**Testes do fechamento:**
- `n0_closure_detects_bypasses` (sintético): detecta desvio por chamada direta, por ponteiro
  e acesso direto ao hardware.
- `n0_fechamento` (executável real): roda a fronteira declarada e a catraca, e mais um **caso
  negativo** em que a fronteira sem uma entrada tem de falhar.

## 6. Interfaces nativas propostas (`native/`)

São C++20 sem nada do Dreamcast: handles tipados de 32 bits, `Result<T>`, sem endereços.

| Interface | Substitui | Etapa | Pontos de migração (entradas da fronteira que alcançam o subsistema) |
|---|---|---|---|
| `files::IFileSystem` (abrir/ler/assíncrono/listar por nome lógico) | GD-ROM, syscall 0xBC do BIOS, gdFs | N1 | 17 |
| `time::IClock`, `time::IEventQueue` (relógio lógico, eventos em ordem fixa) | ciclos, TMU, RTC, VBlank, INTC | N2a/N2b | 31 + as 38 alcançadas pelo runtime |
| `input::IInput` (estado lógico por porta) | Maple, pad | N3 | 5 (Maple também leva o VMU) |
| `saves::ISaveStore` (saves lógicos, versão) | VMU/backup, flash (syscall 0xB8) | N4 | 2 + o VMU sobre Maple |
| `video::IVideoPlayer` (filme convertido; Media Foundation) | Sofdec sobre PVR/AICA | N5 | entradas CRI de vídeo (mwPly/mwSfd), atrás de render/áudio |
| `audio::IAudioEngine` + `IAudioOutput` (bancos/sequências; XAudio2) | AICA, ARM7, `MANATEE.DRV` | N6 | 17 |
| `render::IRenderDevice` + `CommandList` (IR; Direct3D 11) | TA, store queues, VRAM, PowerVR2 | N7a/N7b | 36 |

O teste `n0_native_api` implementa e usa as interfaces (arquivos, render) só com `native/`
no include path.

## 7. Modelo de interoperabilidade (`transitional/`)

O modelo tem três camadas:
1. **Interface nativa:** `native/`.
2. **Adaptador** `Sh4Context` ↔ nativo (`guest_abi.h`):
   - argumentos r4-r7 e depois a pilha; floats em fr4-fr11;
   - retorno em r0/fr0;
   - `GuestMemory` só acessa a RAM principal e recusa hardware, P4, SQ e VRAM;
   - `GuestStruct` tem layout declarado com versão.
3. **Provedor selecionável** (`boundary_registry.h`):
   - legado/nativo por entrada da fronteira, pelo mecanismo dos mods;
   - com slots vale para chamadas diretas e indiretas; sem slots só para as indiretas, e
     `select()` devolve `IndirectOnly`.

Handles de 32 bits com geração (`handle_table.h`):
- ponteiros do host nunca vão para a RAM guest;
- liberar invalida o handle antigo;
- o fim de sessão limpa tudo.

**Custo dos slots** (o plano pede para medir no N0):
- Comparação: build com slots em **todas** as chamadas diretas (`BS_MODS=ON`) contra o build
  normal, com o mesmo roteiro e 4 000 VBlanks, alternados.
- Resultado: normal 20,20/20,09 ms por campo; slots 20,13/20,04 ms. Sem diferença mensurável
  (abaixo de 1 %).
- Ressalva: o rasterizador por software domina o tempo e pode mascarar o custo da CPU. A
  medida tem de ser refeita depois do N7a.
- Conclusão: slots sempre ativos nas fronteiras migráveis são viáveis.

## 8. Threads, reentradas e eventos (seção 5 do plano)

Inventário observado com o trace de ordem no roteiro NEW GAME (3 000 VBlanks).
**Determinístico:** duas execuções dão o mesmo trace (`test_ordem.ps1`, SHA-256 igual).

| Fonte | Observado |
|---|---|
| Interrupções (IRL do Holly) | INTEVT 0x320 (nível 6) ×9 002, 0x360 (nível 4) ×2 982; tratador em VBR+0x600 (0x8C00FA00, lê INTEVT) |
| Bits de evento do Holly modelados | VBlank-In/Out, fim de render (3 bits), fim de DMA do Maple, fim de DMA do PVR, fim de lista do TA (5), fim de YUV |
| Rotina adiada (rte para outro ponto) | 0x8C17DE4C ×2 535 |
| Threads do guest | 3 criadas: 0x8C09081A, 0x8C09081E (biblioteca cooperativa do jogo) e 0x8C158840 (faixa CRI: servidor do Sofdec/ADX) |
| Trocas / longjmp | 14 880 trocas; longjmp ×11 902 para 0x8C0903A8 (retorno do yield) e 0x8C0903F2 (escalonador); setjmp 0x8C099F8C, longjmp 0x8C099F18, 4 pontos de reentrada |
| Callbacks SDK → jogo | 2 alvos indiretos observados pela sonda |

O trace aceito é a referência que o relógio e o agendador nativos (N2) têm de reproduzir na
ordem, não só no estado final.

## 9. Métricas iniciais e catraca

`config/metricas_base_us.toml` é a linha de base. O teste `n0_fechamento` falha se uma chave
com catraca subir:

| Chave com catraca | Base | Meta ao fim |
|---|---|---|
| `hw.refs.camada.jogo` | 0 | 0 |
| `fechamento.violacoes.sdk` / `.hw` | 0 / 0 | 0 / 0 |
| `fechamento.indireto_nao_resolvido` | 1 443 | 0 |
| `camada.desconhecida` / `camada.conflito` | 82 / 1 | 0 / 0 (com identificação por contrato) |
| `pref.nao_resolvido.funcoes.jogo` / `sq.literal.funcoes.jogo` | 0 / 1 | 0 / 0 |
| `fronteira.provedor.legado` | 267 | 0 |

Também ficam registradas, por subsistema, `legado.subsistema.*` (entradas ainda no legado)
e `fronteira.subsistema.*`, que são o que cada etapa baixa.

## 10. Como cada etapa reduz a dependência do Dreamcast

| Etapa | Provedor nativo | Dependências que caem | Medida |
|---|---|---|---|
| N1 Arquivos | `IFileSystem` sobre `data\` | syscall GD-ROM (0xBC), GD-ROM HLE, setores (`RuntimeDisc::readSector`) | `legado.subsistema.arquivos` 17 → 0; leituras normalizadas iguais ao legado |
| N2a/N2b Tempo e tarefas | `IClock`/`IEventQueue`, agendador | VBlank/INTC/TMU/RTC como interrupções; fibras por PC/R15 | `legado.subsistema.tempo` 31 → 0; trace de ordem igual ao aceito; as 38 funções alcançadas pelo runtime passam a ter dono |
| N3 Controles | `IInput` | Maple (pad) | `legado.subsistema.controles` → só o VMU |
| N4 Saves | `ISaveStore` | VMU sobre Maple, flash (0xB8) | `legado.subsistema.saves` 2 → 0 e Maple → 0 |
| N7a Render (IR) | `CommandList` + D3D11 | TA por SQ/DMA, rasterizador por software | SQ para o TA → 0 na sonda; desempenho |
| N5 Vídeo | `IVideoPlayer` (Media Foundation) | Sofdec sobre PVR/AICA | entradas CRI de vídeo → nativo |
| N6 Áudio | `IAudioEngine` + XAudio2 | AICA, RAM de som, `MANATEE.DRV` | `legado.subsistema.audio` 17 → 0; sonda sem AICA |
| N7b Render nativo | Ninja/Kamui nativos | PVR, VRAM, TA | `legado.subsistema.render` 36 → 0 |
| N2c Fim da simulação de tempo | | `BS_POLL` (2 287), ciclos, INTC, periféricos | `gerado.bs_poll` → 0 |
| N8 Sem legado | | `bs_legacy` fora do produto | mapa do linker sem `Sh4Context`/`sh4_read`; `fronteira.provedor.legado = 0` |
| N9 Elevação | `native/game` | código gerado, `sh4_dispatch` (2 601), RAM guest | `gerado.*` → 0 |

## 11. Testes

| Teste | O que garante |
|---|---|
| `unit` (182 casos, 22 novos do N0, 5 da revisão do Codex) | Mapa de regiões; filtro de constantes; varredura (base+deslocamento, pós-incremento, chamada); dono de mensagem; blocos de módulo; fechamento (casos de desvio); fronteira; TOML; catraca; junção da sonda; atribuição da sonda através de `BS_CALL`; agregação de SQ; trace de ordem; handles; `GuestMemory` recusando hardware; convenção de chamada; registro de provedores. |
| `n0_native_api`, `n0_native_limpo` | As interfaces compilam sozinhas e não dependem do Dreamcast. |
| `n0_fechamento` | Fronteira, catraca e caso negativo sobre o executável real. |
| `n0_produto_sem_cdi` | O mapa do linker do `bluestinger.exe` não tem a leitura de CDI. O caso negativo foi conferido à mão com um mapa falso. |
| `tools/dist/test_ordem.ps1` | Ordem determinística e propriedades do trace. |
| Suítes do Builder, da Preview, de gameplay e do projeto | Seção 12. |

## 12. Limitações restantes (riscos mantidos)

Nada abaixo conta como cobertura comprovada:

| Risco | Tamanho | Por que fica | Como reduzir |
|---|---|---|---|
| Alvos indiretos não resolvidos | **1 443** funções do SDK fora da fronteira, com endereço só em tabelas de dado | O dono da tabela é desconhecido: a análise não prova que o jogo os alcança nem que não alcança | sonda em mais roteiros; identificação por contrato; points-to das tabelas. Métrica com catraca. |
| Cobertura dinâmica | 1 roteiro (NEW GAME até a 1ª área, 8 000 VBlanks) | Callbacks e acessos de outras salas, chefes, menus, FMVs e final não foram exercitados | matriz de cobertura (plano, seção 6) |
| Família do SDK | 1 604 das 2 635 funções do SDK sem família | Sem string, assinatura nem banner perto | identificação por contrato em cada etapa |
| Camada | 82 sem camada + 1 conflito (a partida) | Entre a partida e o jogo, no bloco realocado e avulsas | contrato/inspeção; métrica com catraca |
| Premissa "SDK não chama o jogo diretamente" | só a exceção da partida foi vista | Não verificada fora das arestas identificadas (Codex) | sonda: `sonda.indiretas.sdk_para_jogo` (2 observadas) |
| SQ por heurística | 260 prováveis; 12 vistas só pela sonda | `pref` de endereço calculado | sonda; IR de render (N7a) |
| `fmov` sem FPSCR.SZ | acesso registrado com 8 bytes | O modo não é propagado | conservador; a sonda confirma |
| Custo dos slots | sem diferença mensurável hoje | O rasterizador por software domina o tempo | medir de novo depois do N7a |

## 13. Verificação final e pendências

Verificação final (2026-10-15), registrada em `STATUS.md` (N0):
- **Revisão do Codex:** 6 achados, todos aceitos, corrigidos e com testes.
- **Regressão:**
  - 182 unitários;
  - `ctest` 11/11 com o CDI real;
  - Builder 18/18, Preview 9/9, gameplay 4/4, projeto/mods 9/9;
  - trace de ordem 8/8.
- **Comportamento inalterado:**
  - build antes do N0 × build final, quadro depois de exatamente N VBlanks: idêntico bit a
    bit em 1 500 / 3 000 / 4 500 / 6 000;
  - trace de ordem com o mesmo SHA-256 antes e depois das correções.
- **Reprodutibilidade:** fronteira e base de métricas regeneradas byte a byte iguais.

Pendências que **não** são do N0 e dependem de aprovação:
- identificação de API por contrato (N1+);
- roteiros de cobertura além da 1ª área;
- provedores nativos.
