# N2: tempo e eventos nativos (plano técnico e critérios)

Data: 2026-10-15. Autorização do mantenedor: **só o planejamento** do N2, sem alterações
estruturais. Base: N1 (commit `770e3d1`). Plano-mestre: `docs/PLANO-PORT-NATIVO.md`, seções 4
(N2a, N2b, N2c) e 5 (threads, reentradas e eventos). Revisão do Codex: seção 10.

## 1. Situação atual (legado): o tempo é o custo estimado de executar o código

```
código gerado: BS_CYCLES(n) por bloco (58 590 pontos) -> c->cycles
               BS_POLL (2 287 back-edges) a cada 2 000 ciclos -> sh4_service:
                   gdrom_poll         (transferência do GD-ROM, perfil de tempo)
                   holly_intc_sync    (VBlank-In/Out pelas linhas do SPG, fim de render,
                                       fim de DMA do Maple) -> ISTNRM
                   sh4_irq_check      (IRL 6/4/2 pelas máscaras IML -> chamada aninhada ao
                                       tratador em VBR+0x600, INTEVT 0x320/0x360/0x3A0)
               BS_IRQ_CHECK (2 610) depois de `ldc ...,sr` e de `pref`
host:          gancho de VBlank-In -> publica o quadro e segura o ritmo em 60000/1001 Hz
```

Um campo dura **3 336 666 ciclos estimados** (`kFieldCycles` = 200 MHz × 1001/60000). O VBlank
acontece quando o contador passa da linha do SPG, não quando o jogo termina o trabalho do
quadro.

### 1.1 Pontos de entrega de interrupção (onde o SPC é definido)

| Ponto | Origem | Observação |
|---|---|---|
| Poll nos back-edges e no retorno de chamadas (`BS_POLL` → `sh4_service`) | gerador | a cada `kServiceQuantum` = 2 000 ciclos |
| Depois de `ldc …,sr` / `ldc.l …,sr` (`BS_IRQ_CHECK`) | `codegen.cpp` (LDC_SR) | ex.: `8C1C1FD2` → `8C1C1FD4`, `8C157FBE` → `8C157FC0` |
| Depois de `pref` (`BS_IRQ_CHECK`) | `codegen.cpp` (PREF) | fim de lista do TA na hora. Ex.: `pref @r3` em `8C18BCF2`, retomada em `8C18BCF4` |
| Serviço forçado (`next_event = 0`) | `holly_sb.cpp` (fim de DMA do PVR), `native_boot.cpp` | próximo poll entrega |
| Depois do `rte` do tratador | `sh4_irq_check` (laço) | reentrega até não haver fonte pendente |
| `sleep` | `sh4_sleep` → `sh4_service` | 0 ocorrências no código gerado |

### 1.2 Eventos, por tipo

| Tipo | Itens | Onde |
|---|---|---|
| **Agendados** (têm instante devido) | VBlank-In, VBlank-Out (SPG); fim de render (`STARTRENDER` + custo); fim de DMA do Maple (100 ciclos por bit); transferência do GD-ROM (perfil `reios`) | `holly_intc`, `pvr_regs`, `maple`, `gdrom` |
| **Síncronos** (acesos na hora pelo acesso que os causa) | fim de lista do TA (5 bits); fim de YUV (bit 6); fim de DMA do PVR (bit 19, força serviço) | `ta.cpp`, `holly_sb.cpp` |
| **Relógios consultáveis** (sem evento) | TMU canal 0 livre (TCOR0 = TCNT0 = `0xFFFFFFFF`, P/64; underflow **não** modelado); RTC (base do host + ciclos); posição do SPG (`SPG_STATUS`) | `sh4_tmu`, `aica_regs`, `pvr_regs` |
| **Só diagnóstico** | logs com ciclo | `holly_sb`, `vram`, `sh4_threads`, `event_trace` |

### 1.3 Threads, esperas e ritmo

- **Threads do guest:** 3, em fibras do host. Trocas por `rte` com outro R15 e por `longjmp`,
  com 4 pontos de reentrada. A rotina adiada do tratador é `8C17DE4C`. Inventário em
  `docs/N0-RELATORIO.md`, seção 8.
  - O runtime conhece só "correndo" e "suspensa". Suspensa quer dizer chamada C++ pendente,
    não espera de evento.
- **Esperas.** Medidas pelo PC interrompido mais frequente no trace e pelo disassembly:
  - `8C064A20`..`8C064A4C`, código do jogo: espera ativa pela bandeja de 16 bits em
    `0x8C24F88A` (`r14 = 0x8C24D2CC` + `0x25BE`; `mov.w @(r0,r14),r2; tst; bt`). Depois do
    laço, a função faz outras chamadas, decrementa um estado e limpa a bandeja (`8C064A48`).
    **Que a bandeja vem do VBlank ainda não está provado** (N2b exige o trace de quem escreve).
  - `8C1C1FD4` e `8C157FC0`: retomada logo depois de `ldc r0,sr` no SDK.
  - `8C18BCF4`: retomada depois de `pref`.
- **Ritmo observado:** um passo de lógica a cada 2 VBlanks (30 Hz), alternando dois perfis de
  quadro.

## 2. Dependências restantes (Sh4Context, ciclos, VBlank, interrupções, hardware)

| Dependência | Onde | Quem consome | Sai em |
|---|---|---|---|
| `c->cycles`, `c->next_event`, `BS_CYCLES`, `BS_POLL` | `Sh4Context`, código gerado | eventos agendados, relógios, guarda | N2b (o tempo deixa de depender deles); remoção em N2c |
| `SR.IMASK/BL`, `SSR/SPC/SGR`, `VBR`, `INTEVT`, `ret_expect` | `sh4_intc` | entrega como chamada aninhada (seção 1.1) | N2b mantém os pontos necessários (B2); N2c |
| ISTNRM/EXT/ERR, IML2/4/6 | `holly_intc` | tratador do SDK (lê e limpa) | N2a: os bits agendados vêm da fila nativa; N2c |
| SPG (`SPG_LOAD`, `SPG_VBLANK_INT`, `SPG_STATUS`) | `pvr_regs` | SDK, espera de linha | N2a: o VBlank sai do relógio nativo; o registro continua lido |
| TMU, leitores `8C15D1A2` (devolve `0xFFFFFFFF − TCNT0`), `8C15D1AC`, `8C15D1B4`; chamadores como `8C173054`, `8C17683E`, `8C158466`, `8C15BC72` | `sh4_tmu` | Shinobi, CRI, Ninja e o bloco `8C1730xx` | classificar na N2a; semântica entre pontos de espera é a decisão D7 |
| RTC (AICA) | `aica_regs` | relógio do sistema e saves | N2a: `IClock::wall_clock_unix` |
| Fim de DMA do Maple | `maple` | Maple/pad | N3 |
| Fim de render, fim de lista do TA, YUV, DMA do PVR | `pvr_regs`, `ta`, `holly_sb` | Kamui/Ninja/Sofdec | N7a/N7b, N5 |
| Transferência do GD-ROM | `gdrom` | inicialização do gdFs (legado do N1) | N2a usa a fila; sai com o GD-ROM HLE (N8) |
| Fronteira "tempo" | `config/fronteiras_us.toml` | 24 entradas: partida `8C0103A0`..`8C01077E`, `8C08BB40`, `8C08BC92`, CRI ×6, Shinobi `8C15D17C`, Ninja ×2, `8C172D22`..`8C1736A0` | identificadas por contrato em N2a/N2b |
| Threads em fibras por PC/R15 | `sh4_threads` | jogo (escalonador próprio), CRI | N2b (máquina de estados, seção 5) |

**Hardware de tempo no manifesto (N0):**
- 125 referências estáticas, 48 acessos distintos na sonda;
- **0 do jogo**: todo acesso de tempo vem do SDK ou da partida.

## 3. A diferença de 2 VBlanks (N1): fatos e investigação

**Medido em 2026-10-15** (Preview, roteiro NEW GAME, `BS_EVENT_TRACE` + `BS_FILE_LOG`, 300
VBlanks nos dois provedores; os traces ficam fora do repositório e são refeitos pelo mesmo
comando na investigação):
- Os VBlanks caem nos mesmos ciclos nos dois modos.
- Até V40 (cargas do boot), as sequências de eventos por VBlank diferem. As cargas nativas
  terminam ~1 VBlank antes (`CEL000A.NT`…`TPOMMON.PVP` em V32 contra V33).
- De V41 a V220, a sequência de **eventos** por VBlank é idêntica: interrupções, PCs
  interrompidos, trocas de thread, `longjmp`. Isso **não** prova que o estado da RAM seja igual.
- A transição para a tela-título acontece em V221 no legado e em V223 no nativo. Daí em diante,
  tudo fica +2 (`TIT.NT` em V224 × V226).

**Hipóteses** (nenhuma aceita):
- H1: a fase do passo de 30 Hz é fixada no fim das cargas do boot. Uma espera contada em passos
  termina um passo inteiro depois.
- H2: uma espera do boot (logo, fade, som, CRI) conta VBlanks a partir de um evento que mudou,
  arredondada ao passo.
- H3: a inicialização do som ou do CRI consulta o estado do drive (`8C15BFD2`, ~2 M chamadas no
  legado) e avança por contagem de chamadas, não por tempo.

**Método.** Primeiro sem nenhuma intervenção:
1. `BS_RAM_DUMP` em V33, V40, V41, V100, V220 e V221 nos dois modos. Diferença das variáveis do
   jogo, fora dos buffers de arquivo e dos handles.
2. Achar a variável que decide a transição e o **primeiro write divergente** dela. Seguir a
   cadeia de dependência até a causa: `BS_CALL_TRACE` nos escritores e disassembly.

Só depois, como confirmação, uma perturbação **explicitamente rotulada como atraso diagnóstico
artificial**, que nunca vai para o produto. Exemplo: no legado, deslocar o fim de uma carga do
boot em 1 VBlank e ver a transição mudar.

Resultado registrado no relatório do N2.

**Hipótese de convergência** (não é portão obrigatório): se a causa vier do custo estimado de
executar o gdFs legado, o N2b, que separa tempo de custo, deve fazer os dois provedores
convergirem. O critério B4 mede isso; uma divergência que reste só passa com causa na lista
fechada de exceções (seção 5).

## 4. Arquitetura-alvo do N2

```
native/platform/time.h          IClock; IEventQueue revista (D4): publicar/agendar evento tipado,
                                cancelar, reagendar, prioridade fixa por tipo; ITaskScheduler (N2b)
native/platform/clock.*         LogicalClock: instante = (campo, subcampo), taxa 60000/1001,
                                sem acúmulo de erro; tempo do host só para ritmo e medida
native/platform/event_queue.*   EventQueue: ordem total (instante, prioridade do tipo, ordem de
                                agendamento); entrega determinística; produtores internos
                                separados de timers de cliente
native/platform/scheduler.*     (N2b) tarefas cooperativas: apta / bloqueada(evento) / encerrada
transitional/time_adapter.*     ponte com o legado: evento -> ISTNRM/IRL -> tratador VBR+0x600 nos
                                pontos de entrega; TMU/RTC/SPG lidos do relógio; primitiva de
                                espera chamada pelos pontos de espera
```

- **Nada do Dreamcast** em `native/`: sem `Sh4Context`, ciclos, registradores, endereços nem
  INTEVT. O teste `n0_native_limpo` cobre os arquivos novos.
- **Sem interpretador nem emulador:**
  - os pontos de espera são funções recompiladas que chamam uma **primitiva de espera do
    runtime**, abaixo do slot substituível (seção 6, mods);
  - o tratador de interrupção continua sendo código recompilado, chamado por contrato.
- **Provedor selecionável** `--tempo legado|nativo` em todos os builds até o N2c.

## 5. Subetapas e critérios de validação

**Regra geral dos critérios:**
- Toda exceção a uma igualdade fica numa **lista fechada e versionada**
  (`config/n2_excecoes.toml`), escrita antes da execução. Cada item traz causa provada e
  evidência.
- Fora da lista, vale igualdade estrita.
- Cada critério tem **mutação negativa**: um caso que precisa falhar.

### N2a. Relógio lógico e fila de eventos nativos (base de progresso ainda em ciclos)

**O que muda:**
- `LogicalClock` e `EventQueue` implementados.
- Os eventos **agendados** (seção 1.2) passam a ter instante devido na fila nativa.
- Os **síncronos** continuam acesos na hora, pelo mesmo acesso.
- Os **relógios** (TMU, RTC, SPG) passam a ser lidos do relógio lógico.
- O instante lógico é **derivado dos ciclos** por mapeamento exato (campo = ciclos /
  `kFieldCycles`).
- A entrega à CPU **continua nos pontos atuais** (seção 1.1): o adaptador mantém de propósito a
  latência do poll. O comportamento tem de ser idêntico.
- Inventário dos chamadores de `8C15D1A2/1AC/1B4`, cada um classificado como delta por quadro,
  timeout, medição ou passo de simulação. É pré-requisito da decisão D7.

**Critérios:**

| # | Critério | Como | Mutação negativa |
|---|---|---|---|
| A1 | Entrega observada pela CPU idêntica | `BS_EVENT_TRACE` byte a byte (registra INTEVT, SPC, trocas, ciclo de entrega): `--tempo legado` × `nativo`, com cada provedor de arquivos, 3 000 e 8 000 VBlanks | prioridade invertida entre VBlank-In e fim de render → falha |
| A2 | Instante efetivo dos eventos | Trace novo da fila (tipo, instante devido, instante de entrega): o instante devido de VBlank-In/Out bate com a linha do SPG (conferido com `pvr_spg_crossed`); entrega ≥ devido; simultâneos na ordem de prioridade; atraso ≤ 2 quanta. *Ajuste de 2026-10-15:* "1 quantum" subestimava. A entrega é no poll seguinte ao vencimento, e o excesso é o trecho até o próximo ponto de poll: medido até 2 120 ticks. A1 prova que o ponto de entrega é o mesmo do legado. | evento cancelado entregue; deriva; relógio parado → falham |
| A3 | Quadro idêntico | Hash do quadro igual ao do N1 com o mesmo provedor de arquivos (`b49d68e2…` legado em 6 000; `F7818C7E…` nativo em 8 000) | — (A1 cobre) |
| A4 | Relógio e fila | Unitários: simultâneos, cancelar, reagendar, agendar durante a entrega, taxa racional sem deriva em 10⁶ campos | ordem trocada; deriva de 1 campo → falha |
| A5 | Relógios consultáveis | Leituras de TMU/RTC/SPG iguais às do legado no roteiro (log das leituras, valor por leitura) | TMU congelado → falha |
| A6 | Independência | `n0_native_limpo` cobre `clock.*` e `event_queue.*` | include de `Sh4Context` em `native/` → falha |
| A7 | Distribuição | `test_gameplay`, `test_builder`, `test_preview`, `test_projeto`, `test_arquivos`, `test_ordem` | — |
| A8 | Métrica com catraca | `tempo.consumidores.ciclos` = módulos do runtime com leitura **funcional** de `c->cycles` fora do adaptador (diagnóstico conta à parte, `tempo.ciclos.diagnostico`). Tem de cair (meta: só o adaptador) | leitura nova em `maple` → catraca falha |

### N2b. Tarefas e pontos de espera: o tempo deixa de ser custo de execução

**Pré-requisitos (protocolo, plano-mestre seção 5), com evidência de trace e disassembly:**
1. **Máquina de estados por tarefa:** apta, bloqueada(evento), encerrada. Tradução exata de:
   - fibra suspensa;
   - `rte` para outro contexto;
   - `rte` adiado (`8C17DE4C`);
   - os 4 pontos de reentrada por `longjmp`;
   - criação de thread (inclusive dentro de callback).

   "Suspensa" não é "bloqueada": a tarefa só fica bloqueada numa primitiva de espera.
2. **Pontos de entrega** (seção 1.1), cada um classificado como **mantido** ou **removível**.
   Removível só com prova, por trace de `(INTEVT, SPC, tarefa)`, de que o fluxo não muda.
   O ponto depois de `pref` (`8C18BCF2`) e o depois de `ldc sr` ficam **mantidos** até prova em
   contrário.
3. **Pontos de espera identificados por contrato,** com duas evidências cada:
   - `8C064A20`: trace de quem escreve `0x8C24F88A`, com origem no evento ou IRQ, e a cadeia do
     tratador no disassembly. O slot preserva **toda** a função, não só o laço: chamadas,
     decremento de estado, limpeza da bandeja;
   - esperas do SDK por bit de estado (Kamui/TA, Maple, gdFs/CRI) e `sleep`.
4. **Semântica do TMU entre pontos de espera** decidida (D7), com o inventário da N2a.

**O que muda:**
- O relógio lógico avança para o próximo evento quando nenhuma tarefa está apta, todas
  bloqueadas em primitivas de espera. O quadro fica preso ao passo do jogo (D1).
- Os ciclos estimados ficam só como **guarda**: um laço sem ponto de espera conhecido além do
  limite dá erro claro com o PC.
- As interrupções são entregues nos pontos mantidos (item 2) e nas primitivas de espera.
- Tarefas sobre o agendador nativo cooperativo; as fibras continuam como mecanismo do host (D3).

**Critérios:**

| # | Critério | Como | Mutação negativa |
|---|---|---|---|
| B1 | Ordem por passo de lógica | Trace normalizado por passo (tarefa, callback, evento, INTEVT, SPC) igual ao da N2a no roteiro NEW GAME, fora da lista de exceções | ponto de espera removido → falha |
| B2 | Pontos de entrega | Trace de `(INTEVT, SPC, tarefa)` igual nos pontos mantidos | IRQ pós-`pref` de `8C18BCF2` removida → falha |
| B3 | Recursos | Contrato de leituras do N1 (P1) idêntico | — |
| B4 | Provedores de arquivos | Trace de eventos e quadro com `--arquivos legado` × `nativo`: iguais, ou diferença na lista fechada (hipótese da seção 3) | — |
| B5 | Determinismo | Duas execuções: trace e quadro idênticos | provedor com ordem dependente do host (relógio real) → falha |
| B6 | Jogo | `test_gameplay` e snapshots de estado lógico (sala, posição, vida, inventário) nos pontos de controle, iguais à N2a fora da lista | — |
| B7 | Esperas cobertas | Guarda: 0 disparos no roteiro NEW GAME **e** nos roteiros de cobertura disponíveis (menus, FMV, save/load). Sem cobertura, o item fica registrado como risco. Disparo = erro com PC, não laço infinito | espera conhecida sem slot → guarda dispara com o PC |
| B8 | Tarefas | Unitários da máquina de estados: suspensa mas apta, duas tarefas esperando eventos diferentes, criação durante callback, nenhuma apta com fibra suspensa (erro) | estado errado → falha |
| B9 | Desempenho | Trabalho médio por campo (headless, `desempenho:`), 3 execuções alternadas de 4 000 VBlanks na mesma máquina: nativo ≤ legado × 1,05 | — |
| B10 | Distribuição e mods | Todas as suítes; mods de recurso e de função | mod que substitui `8C064A20` sem a primitiva → erro claro (seção 6) |

### N2c. Fim da simulação de tempo (depois de N3, N5, N6 e N7b)

Não executável agora; os critérios ficam definidos:

| # | Critério | Como | Mutação negativa |
|---|---|---|---|
| C1 | Sem hardware de tempo alcançável | `bs-hwscan`: `hw.subsistema.tempo` = 0 no produto; catraca | referência inserida → falha |
| C2 | Sem simulação no executável | Mapa do linker e strings: sem `sh4_intc`, `holly_intc`, `sh4_tmu`, `BS_POLL`, `BS_CYCLES` no produto (`n0_produto_sem_cdi` estendido) | símbolo proibido inserido → falha |
| C3 | Jogo | Matriz de cobertura do plano-mestre (seção 6) com o provedor nativo | — |

**Condição de liberação** (não é teste): remover o legado só depois de uma versão estável
publicada com `--tempo nativo` como padrão.

## 6. Compatibilidade

- **Builder/Preview:** os arquivos novos entram no kit (`kKitEntries`, `make_builder.ps1`,
  `check_kit`). A Preview passa a `--tempo nativo` só depois dos portões de cada subetapa.
- **Mods de função:**
  - o reconhecimento de bloqueio fica **na primitiva de espera do runtime**, chamada pelo código
    da função, não no slot. Um mod que reescreva uma função de espera e chame a primitiva
    continua funcionando;
  - um mod que reescreva a função sem a primitiva faz a guarda disparar com mensagem que nomeia
    a função e a primitiva;
  - a API de mods ganha a primitiva (`bs::mods::wait_event`), documentada no projeto exportado.
- **Mods de recurso:** não são afetados.
- **Saves:** o RTC continua dando data e hora do host. Em roteiros e testes, a data é fixa (D5).

## 7. Rollback e testes diferenciais

- **Rollback:**
  - `--tempo legado` existe em todos os builds até o N2c;
  - as distribuições atuais e os backups ficam intocados (`dist/BlueStinger-Preview`,
    `.anterior`, `.legado-cdi`);
  - um commit por subetapa, só com autorização.
- **Testes:**
  - `tools/dist/test_tempo.ps1` (novo): A1–A5 na N2a; B1–B7 na N2b, com as mutações negativas;
  - `test_ordem` com os dois provedores de tempo;
  - `test_arquivos` em cada modo de tempo;
  - unitários `n2_*`;
  - toda verificação com **build limpo** até a correção D6.

## 8. Riscos

| Risco | Mitigação |
|---|---|
| O jogo depender de lag ou do TMU para medir passo (movimento por delta) | Inventário dos chamadores de `8C15D1A2/1AC/1B4` na N2a; decisão D7 antes do N2b; A5 |
| Espera não identificada trava no N2b | Guarda com erro claro (B7); sonda de PCs interrompidos; roteiros de cobertura; o que não for coberto fica como risco aberto |
| Ordem de eventos simultâneos | Prioridade fixa: hoje VBlank-In, VBlank-Out, render, Maple (`holly_intc.cpp`); A1/A2 |
| Ponto de entrega removido muda SPC e ordem | Pontos mantidos por padrão; remoção só com prova (B2) |
| Mods contornando a espera | Primitiva abaixo do slot; mensagem clara (B10) |
| CRI/Sofdec e áudio cronometrados pelo VBlank | Contratos de eventos de áudio e vídeo; vídeo e áudio nativos são N5/N6 |
| Dependências de build quebradas no ambiente local | Build limpo antes de cada verificação até D6 |

## 9. Decisões que dependem do mantenedor

1. **D1. Modelo de tempo do N2b.**
   - (a) **Quadro preso aos pontos de espera** (recomendado): é o modelo de um jogo de PC, sem
     lag nem tempo de carga emulados.
   - (b) Ciclos estimados como relógio de trabalho: fiel ao lag do Dreamcast, mas preserva uma
     estimativa de custo da CPU original, contra a diretriz.
   - (c) (a) com orçamento por campo como reserva.
2. **D2. Referência depois do N2b:** alinhamento de VBlank, tempo de carga e lag diferentes do
   Dreamcast não contam como regressão, desde que a ordem por passo e o estado lógico confiram
   fora da lista fechada de exceções.
3. **D3. Tarefas:** fibras sob agendador cooperativo determinístico (recomendado). Threads do
   sistema operacional ficam fora: não são determinísticas.
4. **D4. Interface:**
   - revisar `native/platform/time.h`: publicação e agendamento tipados, cancelar, reagendar e
     prioridade por tipo, com produtores internos separados de timers de cliente;
   - acrescentar `ITaskScheduler`;
   - muda uma interface do N0.
5. **D5. RTC:** data do host no jogo normal; data fixa em roteiros e testes.
6. **D6. Ambiente de build:**
   - o ninja perde dependências de cabeçalho: o prefixo `/showIncludes` foi detectado em
     português e a saída chega em outra codificação. Na verificação do N1, isso deixou
     `main_sdl.cpp.obj` velho no build local (STATUS.md);
   - proposta: `VSLANG=1033` nos presets e reconfigurar, no próximo commit autorizado.
7. **D7. Semântica do TMU entre pontos de espera** (depois do inventário da N2a).
   - (a) TMU = relógio lógico do passo + avanço monotônico por leitura dentro do passo: duas
     leituras seguidas nunca são iguais e o delta por quadro é exato.
   - (b) TMU = tempo real do host: não determinístico; fica só para medição.
   - (c) Manter o TMU derivado dos ciclos estimados até o N2c para os consumidores de medição.

## 10. Revisão do Codex (2026-10-15, `reviews/codex-20261015-134106.md`)

Todos os achados foram conferidos no código e no binário antes de entrar:
- `8C15D1A2` = `mov #-1,r0; mov.l @(…),r2; mov.l @r2,r3; rts; sub r3,r0`;
- `BS_IRQ_CHECK` depois de `PREF` e `LDC_SR` em `codegen.cpp`;
- `next_event = 0` em `holly_sb.cpp`;
- TMU sem underflow modelado.

| Gravidade | Achado | Decisão |
|---|---|---|
| ALTA | N2b omitia pontos de entrega (pós-`pref`, polls, serviço forçado) | **Aceito.** Seção 1.1; pré-requisito 2 do N2b; B2 com mutação em `8C18BCF2` |
| ALTA | Congelar o relógio durante o trabalho muda o TMU (`8C15D1A2` lê TCNT0) | **Aceito.** Inventário dos chamadores na N2a; D7; A5 |
| ALTA | "Todas bloqueadas" indefinido para fibras, `rte` e `longjmp` | **Aceito.** Máquina de estados no N2b; B8 com os casos negativos |
| ALTA | A1 byte a byte é incompatível com entrega no instante exato | **Aceito.** N2a mantém a latência de entrega; A1 (entrega observada) e A2 (instante efetivo) separados |
| MÉDIA | `IEventQueue` sem agendamento tipado | **Aceito.** D4 |
| MÉDIA | Inventário incompleto (TA, YUV, DMA do PVR, diagnóstico; TMU/RTC não são eventos) | **Aceito.** Seção 1.2; A8 separa diagnóstico |
| MÉDIA | Espera `8C064A26` ainda não provada como VBlank | **Aceito.** Pré-requisito 3: quem escreve `0x8C24F88A`; slot preserva a função inteira |
| MÉDIA | Investigação concluía antes de provar e usava atraso | **Aceito.** Seção 3: método sem intervenção primeiro; perturbação rotulada; convergência é hipótese (B4) |
| MÉDIA | Exceções abertas e poucos casos negativos | **Aceito.** Lista fechada versionada; coluna de mutação negativa; B9 com tolerância; C4 virou condição de liberação |
| MÉDIA | Mods podem contornar os slots de espera | **Aceito.** Primitiva abaixo do slot; B10 com mod negativo |

## 11. Pendências trazidas de outras etapas

### 11.1 Tempo do download de música para a RAM de som (do N6-F, 2026-10-20; decisão do mantenedor: opção C)

O `8C15D534` (download de SMPB/SMSB e das unidades do `BS_SND.MLT`) no original deixa o SH-4 esperando o DMA para a RAM de
som, linear no tamanho: ~177 ns/byte (~5,66 MB/s), medido no Flycast (`tools/dev/n6_oraculo.py --musica`, entrada e volta
da função com o TMU0): 85,5 ms para o banco da M2A (483 680 bytes), 2,5 ms para a sequência (13 984). Com
`--audio-musica nativo` o download devolve na hora; o legado (DMA emulado) também não segue o original. No jogo: o estado
lógico por passo é o mesmo, mas as cargas acabam 1–2 VBlanks antes (boot e troca de sala da M2A). **Não resolvido:** a
espera (andar os ciclos do contexto e atender os eventos como o laço gerado faz) é do modelo de tempo deste N2, retomado
depois do N7 e antes do N8; até lá a música nativa não tem fidelidade temporal completa nas cargas. Detalhes em
`docs/N6-PLANO.md` seção 18.5.

Consequência audível medida depois (Flycast, troca M2A → M2C pelo carregador do jogo `8C02A660`): o carregador manda
`vol −113 t0, pan, parar` **antes** dos downloads e `vol −16 t0, tocar, vol −16 t2000` **depois**. No original, a espera
do DMA (~66 ms) deixa o ARM7 aplicar o primeiro lote antes do segundo: a porta vai a −113 e o `t2000` faz um **fade de
entrada** a partir dali (1,55 s; os 18 primeiros key-on da M2C no oráculo têm TL 141–190). No nativo o download é
instantâneo, os dois lotes caem no mesmo quadro e a música nova começa direto em −16. Some quando a espera do download
for reproduzida (este item); não foi compensada no N6.

O mesmo efeito na **M3** (2026-10-21, `--injetar-musica 2700:3` × `bs-musica M3`): os 48 key-on que não pareiam com
todos os registradores estão todos no 1º 1,4 s, com o TL do original 94 acima do nativo (a porta ainda em −113); fora
disso, 3 207/3 255 com todos os registradores e 3 255/3 255 em amostra e tom (janela de 100 ms).

### 11.2 Desempenho do gameplay e o áudio em tempo real (do N6-H, 2026-10-21)

Na janela, o gameplay leva em média **~18,4 ms por campo** nesta máquina (42 % dos campos acima de 16,68 ms, picos de
~140 ms), **igual com o áudio legado e o nativo** na mesma rota (18,36 × 18,16 ms headless; já medido no N2: 17,23 ms no
estável e 14,75 ms no experimental). O áudio nativo manda um VBlank de mistura por campo; abaixo do tempo real a fila da
saída esvazia e o dispositivo toca silêncio (faltas de dados). A saída do jogo ganhou uma reserva de 66 ms (N6-H,
`docs/N6-PLANO.md` 20.3), que absorve campos atrasados isolados (faltas 1 399 → 345 em 7 200 blocos), mas não um jogo mais
lento que o tempo real na média. **O critério H9 do N6 (0 faltas de dados) fica bloqueado por este item**: só passa com o
desempenho do gameplay dentro do tempo real (o provedor experimental, −12 a −14 %, ou outra melhoria do modelo de tempo),
retomado depois do N7 e antes do N8, junto com o 11.1. O limite do H9 não foi afrouxado.

## 12. Retomada do N2 (2026-10-23, depois do fechamento do N7)

Prioridade do mantenedor: temporização, desempenho e as faltas de áudio que bloqueavam o H9 do N6.

### 12.1 Onde estava o tempo do gameplay

Rota LOAD (save g1, `passo_load_slot1.txt`), headless, 6 000 VBlanks, áudio nativo, Preview `465b2ed`, RTX 5080, duas
rodadas alternadas (`perf4.ps1`):

| Renderizador e tempo | Trabalho por campo | Tempo total |
|---|---|---|
| software, estável | 22,99 / 22,97 ms | 141 s |
| Vulkan, estável | 7,47 / 7,49 ms | 48 s |
| software, experimental | 20,32 / 20,51 ms | 125–126 s |
| Vulkan, experimental | 4,63 / 3,84 ms | 26–31 s |

- O **rasterizador em software custa ~15 ms por campo** no gameplay: era ele que deixava o jogo abaixo do tempo real
  (11.2), e não o modelo de tempo nem o áudio.
- O **tempo experimental** tira mais 2,8–3,6 ms (a thread 3 deixa de girar sem efeito), mas não resolve o software.

### 12.2 H9 (11.2) resolvido pelo Vulkan padrão

Na janela, com o Vulkan padrão, o H9 passou 3 vezes seguidas (0 faltas; 7,14–7,22 ms por campo); com o software, no
mesmo executável e rota, 287 faltas (17,11 ms). Detalhes: `docs/N6-PLANO.md` 20.6. Com o tempo experimental e o Vulkan
(janela): 5,45 ms por campo, mas **1 falta**: as cargas instantâneas (D11) concentram o trabalho num campo só (pico de
63 ms). O experimental não é, por si, melhor para a continuidade do áudio.

### 12.3 Pendências de desempenho registradas

1. **Espera síncrona da GPU na thread do jogo.** Cada render espera a cerca e a fila ociosa (`vk_device.cpp`): 1,6 ms
   por render a 480p, 5,4–5,8 ms a 4K (pior 85 ms). A 4K com efeitos isso dá 1 falta em 150 s. Tentativa feita e
   **revertida**: trocar o `vkQueueWaitIdle` por um carimbo do quadro no cabeçalho (sem esperar o trabalho da
   apresentação) não melhorou (480p: 3,14 ms de espera e 8,22 ms por campo; 4K: 5,43 ms e um pico de 2 s). A solução de
   verdade é não devolver a imagem à CPU quando a apresentação usa o slot da GPU (leitura assíncrona), uma mudança de
   arquitetura do renderizador a decidir com o mantenedor.
2. **Recuo para o software:** continua abaixo do tempo real no gameplay (~23 ms por campo headless). Otimizar o
   rasterizador de referência tem valor limitado (o N8 decide o destino dele).
3. **11.1 (download da música):** continua aberto; é o próximo item de temporização.
4. **Promoção do tempo experimental** (U5–U8 e a cobertura de troca de sala, combate, save/load e finais): continua
   aberta; o ganho medido aqui (−38 % no Vulkan) é um argumento a favor, e as cargas concentradas num campo, um ponto a
   tratar antes.
