# N2a: relógio lógico e fila de eventos nativos (relatório)

Data: 2026-10-15. Plano e critérios: `docs/N2-PLANO.md`, seção 5. Base: N1 (`770e3d1`).
Autorização do mantenedor: só o N2a. Decisões aplicadas: D1–D6; a D7 fica aberta (seção 6).
Sem commit. N2b e N2c não foram iniciados. **Estado: aprovado pelo mantenedor em 2026-10-15
(commit único autorizado).**

## 1. Resumo

| Pergunta | Resposta (com evidência) |
|---|---|
| Os eventos de tempo vêm de um sistema nativo? | **Sim, com `--tempo nativo` (padrão).** VBlank-In/Out, fim de render, fim de DMA do Maple e blocos do GD-ROM são agendados numa `EventQueue` nativa. TMU, SPG_STATUS, MDST e RTC leem um `LogicalClock` nativo. |
| O comportamento mudou? | **Não.** Em 8 000 VBlanks do roteiro NEW GAME, com os dois provedores de arquivos: mesma ordem de eventos (interrupções com SPC, trocas de thread, `longjmp`), mesmas leituras de relógio e de estado, mesmo quadro, igual à referência do N1 (seção 3). |
| Os ciclos estimados ainda contam? | **Só como entrada de compatibilidade.** É o instante que o adaptador passa ao relógio: um tick por ciclo (`kCompatTicksPerSecond`/`kCompatTicksPerField`). Nenhum módulo do runtime mede tempo por ciclos: `tempo.consumidores.ciclos` caiu de 7 para **0**, com catraca. A base de progresso (`BS_CYCLES`/`BS_POLL`) continua: é o N2b. |
| Rollback | `--tempo legado`: o cálculo de antes, reunido sem mudança em `runtime/cpu/time_legacy.cpp`. |
| Diferença de 2 VBlanks (N1) | **Causa provada por estado e traces, sem intervenção** (seção 4): interrupções de VBlank fundidas por um trecho com interrupções mascaradas, no boot. |
| Eliminação do hardware original | **Não concluída.** A entrega ainda é por interrupção e o progresso é por ciclos (seção 7). |

## 2. Implementação

| Peça | Onde | Dreamcast? |
|---|---|---|
| Interface | `native/platform/time.h`: `Instant` em ticks, `TimeBase` inteira, `EventKind` com prioridade, `IEventQueue` (`post`/`cancel`/`reschedule`/`subscribe`/`schedule_timer`/`run_until`/`set_kind_priority`; cancelar uma inscrição vale na hora, inclusive durante a entrega), `IClock`, `ITaskScheduler` só declarada (N2b) | não (`n0_native_limpo`) |
| Relógio | `native/platform/clock.*` (`LogicalClock`: monotônico; hora de parede = valor num instante de referência + tempo lógico decorrido, sem decorrer antes da referência) | não |
| Fila | `native/platform/event_queue.*` (ordem total: instante, prioridade do tipo, ordem de agendamento; eventos agendados durante a entrega entram na mesma passagem) | não |
| Fronteira no runtime | `runtime/cpu/time_hooks.h` (`TimeProvider`, `time_now`); módulos: `holly_intc`, `pvr_regs`, `maple`, `gdrom`, `sh4_tmu`, `aica_regs`, `sh4_intc`, `sh4_runtime` | legado |
| Provedor legado | `runtime/cpu/time_legacy.cpp` | legado (rollback) |
| Provedor nativo | `transitional/time_adapter.*`: `LogicalClock` + duas `EventQueue` (Holly/PVR/Maple e GD-ROM, cada uma sincronizada nos pontos de antes) | só a ponte |
| Seleção | `--tempo nativo` (padrão) \| `legado` | — |
| Relógio de parede (D5) | hora local do host no boot; `BS_RELOGIO_FIXO=<segundos Unix>` em testes e roteiros | — |
| Diagnóstico | `BS_TIME_TRACE` (cada entrega: tipo, devido, entrega e se o VBlank cai na linha do SPG); `BS_CLOCK_LOG` (por VBlank: quantidade e hash das leituras de TMU, SPG_STATUS, MDST, RTC e ISTNRM/EXT/ERR); `BS_TEMPO_MUTACAO` (só testes negativos) | — |

Decisões de implementação:
- **Entrega nos pontos atuais.** O adaptador conserva de propósito a latência do poll (revisão
  do Codex do plano, A1×A2). Por isso o trace de ordem pode ser comparado byte a byte.
- **Vários VBlanks no mesmo intervalo** acendem o bit uma vez só, como antes. A fila entrega
  cada um, e o trace da fila registra todos.
- **Coerência dos módulos.** A configuração do SPG que muda (escrita em `SPG_LOAD` ou
  `SPG_VBLANK_INT`, carga dos registradores, reset) reagenda os VBlanks a partir do último
  instante entregue, a mesma referência do cálculo preguiçoso.
  - Um novo STARTRENDER reagenda o fim de render.
  - O MDST que vê o fim do Maple antes cancela o evento.
  - Um pedido novo do GD-ROM cancela o bloco pendente.

## 3. Portões (N2a)

Matriz do `tools/dist/test_tempo.ps1`: roteiro NEW GAME, relógio de parede fixo, `--tempo legado`
× `--tempo nativo`.

| # | Critério | Resultado |
|---|---|---|
| A1 | Ordem observada idêntica (`BS_EVENT_TRACE`, byte a byte) | **ok** em 3 000 VBlanks com arquivos nativo (`176F4C28…`), 6 000 com arquivos legado (`D81D26E8…`) e 8 000 com arquivos nativo (`A254488E…`) e legado (`FCAF109E…`) |
| A2 | Instante efetivo (`BS_TIME_TRACE`) | **ok**, 8 000 VBlanks, os dois provedores de arquivos: 15 999 VBlanks com o devido **exatamente na linha do SPG** (conferido por `pvr_spg_crossed`); 0 entregas antes do devido (27 966 / 27 967); 0 pares simultâneos; atraso máximo **2 120 ticks** (limite 4 000, critério corrigido no plano) |
| A3 | Quadro | **ok**: idêntico entre os provedores de tempo em toda a matriz; igual ao N0 em 6 000 (`B49D68E2165B8505`) e ao N1 em 8 000 (`F7818C7E…` nativo, `F472ECC2…` legado) |
| A4 | Relógio e fila | **ok**: 12 unitários `n2_*` |
| A5 | Relógios consultáveis e estado | **ok**: leituras de TMU, SPG_STATUS, MDST, RTC e ISTNRM/EXT/ERR idênticas por VBlank em toda a matriz. Volume: TMU ~3 400 leituras por campo, SPG ~480 |
| A6 | Independência | **ok**: `n0_native_limpo` |
| A7 | Distribuição | seção 9 |
| A8 | Métrica | **ok**: `tempo.consumidores.ciclos` 7 → 0 e `tempo.ciclos.funcionais` 21 → 0, com catraca. Restam: entrada de compatibilidade 1 (`time_now`), progresso 3 (as duas linhas das macros e o quantum), diagnóstico 11 |

**Casos negativos.** Cada defeito é introduzido de propósito e tem de ser detectado; no
`test_tempo`, o mutante precisa terminar normalmente (rc 0) e divergir no artefato esperado.

| Defeito | Onde | Detectado em |
|---|---|---|
| Prioridade invertida (comparador real da fila, eventos já pendentes) | `n2_queue_orders_simultaneous_events_by_kind_then_sequence` | ordem de entrega |
| Evento perdido (`perder_vblank_out`) | unitário; `test_tempo` | leituras de ISTNRM |
| Evento atrasado (`atrasar_render`) | unitário; `test_tempo` | ordem |
| Evento cancelado entregue (`ignorar_cancelamento`) | `n2_mutations_are_detected` | vencimentos de render e Maple. Não está no `test_tempo`: o roteiro não cancela nenhum evento (`cancelados 0`) |
| Deriva do VBlank (`deriva`) | unitário; `test_tempo` | ordem |
| Relógio em degraus (`relogio_degraus`) | unitário; `test_tempo` | leituras de TMU/SPG |
| Mutação desconhecida | unitário | recusada |
| Leitura de ciclos fora do provedor (Maple, `sh4_runtime.cpp`, `time_hooks.h`) | `n0_fechamento` | catraca (rc 4) |
| Dependências de cabeçalho perdidas | `n2_build_incremental` | prefixo trocado |

Duas lições ficaram registradas:
- Na primeira versão, perder VBlank-Out **não** foi detectado. O jogo não trata essa
  interrupção, e o trace só via o que a CPU executava. As leituras de ISTNRM/EXT/ERR entraram no
  `BS_CLOCK_LOG`.
- A primeira mutação de relógio parado **travou o jogo**: o poll central lê o instante pelo
  mesmo relógio, então nenhum VBlank voltava a vencer. Foi trocada por `relogio_degraus`, e
  cada execução do `test_tempo` ganhou tempo-limite.

## 4. A diferença de 2 VBlanks (N1): causa

Método:
- só observação, nos dois provedores de arquivos, com o mesmo roteiro e relógio fixo;
- snapshots de RAM (`BS_RAM_DUMP`) em V20–V33, V40, V41, V100, V220 e V221;
- trace de ordem (`BS_EVENT_TRACE`);
- trace de chamadas (`BS_CALL_TRACE`) das funções de troca de quadro do SDK;
- disassembly.

**Cadeia causal:**
1. **V20–V22:** no modo de arquivos nativo, a carga de `BS_SND.MLT` termina antes, e um trecho
   de trabalho com interrupções mascaradas começa em outra fase do campo.
   - A interrupção do VBlank 20 só é aceita 2,7 M ciclos depois (0,8 campo; retomada em
     `8C17CDB0`).
   - A do VBlank 21 atrasa ~1,4 campo e é atendida junto com a do VBlank 22: um atendimento para
     dois VBlank-In (trace: nenhum `I` entre V21 e V22).
   - No legado, o mesmo trecho cai noutra fase, e cada VBlank tem o seu atendimento.
2. **V22:** primeira escrita divergente. O contador de quadros do SDK em `0x8C2B4488` passa a 5
   em V22 no legado e só em V23 no nativo. Os ponteiros de buffer duplo (`0x8C2B4250` etc.)
   ficam em fase oposta daí em diante.
   - Quem escreve: as funções de troca de quadro `8C08F078`, `8C08F0E0` (Shinobi) e `8C08F332`
     (Kamui), com o literal `0x8C2B4488` nos pools `8C08F0C0`/`8C08F1CC`/`8C08F2FC`/`8C08F430`.
   - Até V30: 12 chamadas a `8C08F332` no legado, 11 no nativo.
3. **V40–V220:** as variáveis do jogo (`8C24xxxx`) e a sequência de eventos por VBlank são
   iguais nos dois modos. Os contadores do SDK seguem 1 à frente no legado (por exemplo 0xCA ×
   0xC9 em V220).
4. **V221/V223:** com o passo de lógica de 2 VBlanks, a condição de transição ligada a esses
   contadores é atingida um passo antes no legado. A tela-título entra em V221 (legado) e
   V223 (nativo), e daí em diante tudo fica +2.

**Conclusão:**
- Não é defeito do provedor nativo de arquivos. A fusão de interrupções de VBlank quando a CPU
  fica mascarada por mais de um campo acontece também no hardware.
- A fase em que o trabalho mascarado cai depende do custo estimado de execução, que muda quando
  o gdFs legado deixa de rodar.
- No N2b, o tempo deixa de andar durante o trabalho (D1). Nenhum VBlank cai dentro de um trecho
  mascarado, e a fusão desaparece nos dois provedores.
- Isso sustenta a hipótese de convergência (B4 do plano). Ela continua hipótese até ser medida
  no N2b.

## 5. Consumidores do TMU (inventário para a D7)

- A API é o temporizador do Shinobi (syTmr):
  - `8C15D17C` inicia o canal 0 (P/64, contagem a partir de `0xFFFFFFFF`);
  - `8C15D1A2` devolve a contagem decorrida;
  - `8C15D1AC` faz a diferença entre duas contagens;
  - `8C15D1B4` converte contagem em microssegundos.
- Estático: 30 pontos de chamada em 14 funções.
- Dinâmico (`BS_CALL_TRACE`, NEW GAME, 3 000 VBlanks): 20,4 milhões de chamadas.

**Nenhum chamador é do jogo:** todos são do SDK.

| Chamador | Família | Chamadas (3 000 VBlanks) | Uso (disassembly) | Classe |
|---|---|---|---|---|
| `8C1586B8` | CRI | 20,0 M (contagem, diferença, µs) | mede quanto durou uma chamada do servidor e acumula em global se passar de um limite | **medição** (carga do servidor) |
| `8C173044` | sem família (faixa da fronteira "tempo") | 334 mil | espera uma bandeira (`[0x8C17311C]`) com **timeout** em µs (`[0x8C173120]`); no estouro chama `8C172FE6` | **timeout de espera** |
| `8C1799DC` | CRI | 36 mil | mede várias etapas em sequência e guarda as durações | **medição** (perfil) |
| `8C158864`, `8C158A26` | CRI | 4,5 mil / 3 mil | contagem lida (a classificar) | a classificar |
| `8C08E126`, `8C096C5C`, `8C0978BE` | CRI / sem família | 2,8 mil / 48 / 0 | contagem guardada em estrutura (a classificar) | a classificar |
| `8C1766C0`, `8C1769E6`, `8C196EE4`, `8C1583FA`, `8C15BC26` (gdFsStop) | sem família / CRI / Shinobi | ≤ 140 | timeout ou medição (a classificar) | a classificar |
| `8C078400`, `8C08BD00` | — | 1 cada | `8C15D17C` (início do temporizador) | inicialização |

**Recomendação para a D7** (decisão do mantenedor; nada foi implementado):
1. **Medição:** o TMU segue o relógio lógico. Antes, provar com trace de escrita e leitura que
   os acumulados (por exemplo o global somado por `8C1586B8`) não alimentam decisões. Se não
   alimentarem, o valor medido não muda o jogo, e tempo zero dentro do passo é equivalente.
2. **Timeout de espera** (`8C173044` e os que a classificação achar): tratar como ponto de
   espera do N2b.
   - Bloquear até o evento que acende a bandeira **ou** até um temporizador nativo
     (`schedule_timer`) no instante do timeout. É o mesmo limite em tempo lógico, sem avanço
     artificial por leitura.
   - A equivalência se demonstra comparando com o legado o caminho tomado (bandeira ou
     timeout) e o valor da bandeira.
3. Sem tempo real do host e sem incremento por leitura, como pedido. A opção (a) do plano
   (avanço por leitura) fica descartada, salvo se um consumidor exigir e isso for demonstrado.

## 6. Métricas de dependência legada (catraca: `config/metricas_base_us.toml`)

| Métrica | N1 | N2a |
|---|---|---|
| `tempo.consumidores.ciclos` (módulos que medem tempo por ciclos) | 7 | **0** (catraca) |
| `tempo.ciclos.funcionais` (leituras) | 21 | **0** |
| `tempo.ciclos.entrada_compat` | — | 1 (`time_now`) |
| `tempo.ciclos.progresso` | 4 | 3 (`BS_CYCLES`, `BS_POLL`, quantum) |
| `tempo.ciclos.diagnostico` | 11 | 11 |
| `fronteira.provedor.legado` | 262 | 262 (o N2a não troca entradas da fronteira do jogo) |
| `legado.subsistema.tempo` | 24 | 24 |
| `hw.subsistema.tempo` (referências estáticas) | 125 | 125 |
| Código gerado: `BS_POLL` / `BS_CYCLES` / `BS_IRQ_CHECK` | 2 287 / 58 590 / 2 610 | iguais (N2b/N2c) |

## 7. Caminhos que continuam legados

| Caminho | Sai em |
|---|---|
| Progresso por ciclos estimados (`BS_CYCLES`, `BS_POLL`, quantum) | N2b |
| Entrega como interrupção (INTC, IRL, tratador em VBR+0x600, máscaras) | N2b (pontos de espera) / N2c |
| Registradores de tempo (TMU, SPG, ISTNRM, RTC do AICA) como interface do SDK | N2c, depois de N3/N5/N6/N7b |
| Threads do guest em fibras por PC/R15 | N2b |
| Eventos síncronos do TA, YUV e DMA do PVR | N7a/N5 |
| GD-ROM HLE (perfil reios), boot | N8 |

## 8. D6: ambiente de build

- `CMakePresets.json`: `VSLANG=1033`. **Não resolve nesta máquina**: sem o pacote de idioma
  inglês do Visual Studio, o `cl` continua em português (conferido).
- Causa real: o prefixo `/showIncludes` foi detectado com a página de código 65001 (PowerShell),
  e builds disparados de um terminal com página 850 (Git Bash) gravam o prefixo em outra
  codificação. O ninja então perde as dependências.
- Medido: depois de um build limpo do PowerShell, 0 objetos do projeto ficam sem dependências;
  depois de builds mistos, 110 a 120.
- Proteções:
  - aviso no configure quando o prefixo é localizado;
  - teste `n2_build_incremental`, que reprova objetos do projeto sem dependências, confere que
    mudar um cabeçalho recompila e tem caso negativo com prefixo trocado.
- Solução completa: instalar o pacote de idioma inglês (decisão do mantenedor), ou compilar
  sempre do mesmo tipo de terminal.

## 9. Verificação

Em 2026-10-15, no código final, depois das correções das duas revisões do Codex:

| Verificação | Resultado |
|---|---|
| Unitários | 204/204 (12 `n2_*`). Os testes das correções de RTC, `unsubscribe` e `INIT_SYSTEM` falham com a correção revertida |
| `ctest` com o CDI real | 12/12 (`boot_trace` pulado: sem o oráculo) |
| `n0_fechamento` | catraca ok; negativos de fronteira e de leitura de ciclos (Maple, `sh4_runtime.cpp`, `time_hooks.h`) detectados |
| `n2_build_incremental` | ok (0 objetos do projeto sem dependências; o cabeçalho recompila; o negativo é detectado) |
| `make_builder`, `make_preview` | ok. A Preview foi compilada do zero, com o cache do Builder renomeado. Conferido no executável distribuído: aceita `relogio_degraus` e recusa `congelar_relogio` |
| `test_preview` | 9/9 |
| `test_gameplay` (NEW GAME até a 1ª área, tempo e arquivos nativos por padrão) | 4/4 |
| `test_builder` | 18/18 |
| `test_projeto` (projeto editável e mods) | 11/11 |
| `test_arquivos` (P1/P2/P3/P8 do N1) | 6/6 |
| `test_ordem` | 8/8 |
| `test_tempo` (matriz A1/A2/A3/A5 e 4 negativos) | 35/35 |

Rollback preservado:
- `--tempo legado` e `--arquivos legado`;
- `dist/BlueStinger-Preview.anterior` (rotação do `make_preview`);
- `dist/BlueStinger-Preview.n1-770e3d1` (executável do N1);
- `dist/BlueStinger-Preview.legado-cdi`;
- caches antigos do Builder em `%LOCALAPPDATA%\BlueStingerRecomp\build.antes-n2a` e `build.n2a-1`.

## 10. Riscos restantes

- **D7 em aberto:** 10 chamadores do TMU ainda sem classe; o timeout de `8C173044` precisa virar
  ponto de espera no N2b.
- A fusão de VBlanks por trabalho mascarado mostra que **o lag do boot muda a fase lógica**.
  O N2b (D1) remove a causa, mas o N2b passa a ter outra referência de tempo (D2).
- Cobertura: o roteiro NEW GAME até a 1ª área. Caminhos de tempo não exercitados (perfil
  `reios`, SPG entrelaçado, Maple com resposta longa) estão cobertos só por unitários.
- A entrega continua dependente do quantum de poll: atraso máximo medido de 2 120 ticks.

## 11. Revisões do Codex

**Revisão do N2a** (`reviews/codex-20261015-142429.md`, somente leitura): 4 achados MÉDIA e 3
BAIXA. Todos foram conferidos no código e aceitos. Cada correção tem um teste que falha sem
ela: as três primeiras foram conferidas revertendo a correção e vendo o teste falhar.

| Gravidade | Achado | Correção | Teste |
|---|---|---|---|
| MÉDIA | O reset do provedor nativo zerava a base do RTC; o legado a preserva | `rtc_set` guarda a hora e a referência; `start()` reaplica com `LogicalClock::set_wall_clock(segundos, instante)`. Referência no futuro conta 0 decorrido, como no legado | `n2_rtc_survives_reset_like_legacy` (volta dos 32 bits, reset para trás) |
| MÉDIA | O A2 não conferia a linha do SPG e aceitava o dobro do limite do plano | Trace da fila com a coluna `linha`, conferida por `pvr_spg_crossed` (a conta do legado, independente de `pvr_spg_next_crossing`); `test_tempo` confere também os simultâneos. O limite de 4 000 ficou, **por evidência**: o atraso de entrega é propriedade do poll do legado (medido até 2 120), e o A1 prova que o ponto de entrega é o mesmo. O critério do plano foi corrigido | `test_tempo` (CheckQueue) |
| MÉDIA | Faltavam mutações reais; um crash contava como detecção | `IEventQueue::set_kind_priority` (mutação real do comparador); mutações `ignorar_cancelamento`, `deriva` e `congelar_relogio`; o negativo exige rc 0 e divergência no artefato esperado | `n2_queue_orders…` (prioridade), `n2_mutations_are_detected`, `test_tempo` |
| MÉDIA | A matriz do A3 (3 000/6 000) não rodava | `test_tempo`: 3 000 (arquivos nativo), 6 000 (arquivos legado, quadro do N0 `B49D68E2165B8505`), 8 000 (os dois, quadros do N1) | `test_tempo` |
| BAIXA | `INIT_SYSTEM` deixava um bloco do GD-ROM na fila | `disc_cancel()` em `INIT_SYSTEM` (o provedor legado também guardava o estado) | `n2_gdrom_init_system_cancels_pending_block` |
| BAIXA | A exceção "progresso" da catraca valia para o arquivo inteiro | Só as expressões conhecidas ficam isentas; qualquer outra leitura nesses arquivos é consumo funcional | `n0_fechamento`: negativos em `maple.cpp`, `sh4_runtime.cpp` e `time_hooks.h` |
| BAIXA | Cancelar uma inscrição durante a entrega não tinha efeito imediato | Contrato definido: vale na hora; uma inscrição nova só vê os próximos eventos | `n2_queue_unsubscribe_takes_effect_during_delivery` |

**Revisão de acompanhamento** (`reviews/codex-20261015-164027.md`): **nenhum achado funcional
novo**. As sete correções fecham os achados sem quebrar o provedor legado. A justificativa do
limite do A2 se sustenta no código (`BS_POLL`/`sh4_service`).
- Observação de cobertura, aceita: o teste de prioridade mudava a prioridade antes de agendar.
  Agora muda depois, provando a reordenação dos eventos já pendentes.
- Depois da revisão, a mutação `congelar_relogio` foi trocada por `relogio_degraus` (seção 3).
  A mudança fica só no caminho de teste, desligado por padrão.
