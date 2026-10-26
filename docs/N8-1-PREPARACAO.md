# N8.1 (N2c) — preparação técnica

**Situação (2026-10-25): N8.1 autorizado e em andamento** (andamento na seção 7). As seções 1–6 são o levantamento
feito durante o N8.0. Referências: `docs/N8-PLANO.md` (L3–L8 e a seção 5), `docs/N2-PLANO.md`
14, `docs/N8-0-REFERENCIAS.md` (referências e comparador). As diretrizes aprovadas em 2026-10-24 estão na seção 4.1; o
relatório técnico completo, na seção 6.

## 1. Consumidores levantados

### 1.1 Código gerado (`generated/`, 12 arquivos, contagem por texto)

| Macro / chamada | Ocorrências | Papel hoje |
|---|---|---|
| `BS_CYCLES(n)` | 58 590 | soma ciclos estimados de cada trecho linear (`c->cycles`) |
| `BS_POLL(pc)` | 2 272 (back-edges) | `++polls`; se `cycles >= next_event`: `sh4_service` |
| `BS_CALL(...)` | 14 987 (cada um com `BS_POLL_RET`) | o mesmo poll no retorno de toda chamada direta |
| `BS_IRQ_CHECK(pc)` | 2 610 | depois de `ldc ...,sr`: entrega IRQ desmascarada agora |
| `BS_WAIT` / `BS_WAIT_END` | 15 / 14 | pontos de espera declarados (N2b-2) |
| `sh4_dispatch` | 2 601 | despacho indireto (substrato AOT; fica até o N10) |

### 1.2 Runtime (`c->cycles` fora de diagnóstico)

| Onde | Uso funcional |
|---|---|
| `sh4_runtime.cpp` `sh4_service` | cadência do poll: `next_event = cycles + kServiceQuantum` (2 000) |
| `time_legacy.cpp`, `time_adapter.cpp` (estável) | o relógio **é** os ciclos (`time_now`) |
| `pvr_regs.cpp` `pvr_spg_position(cycles)` | linha do SPG pelo instante (o experimental já passa o instante lógico) |
| `sh4_threads.cpp`, `sh4_intc.cpp`, `pvr_regs`, `vram`, `holly_sb`, `memory` | só traces e logs de diagnóstico |

### 1.3 Hardware de tempo alcançado pelo jogo (manifesto `bs-hwscan`, sonda dinâmica)

| Região | Função do guest | Acesso | Leituras/escritas na rota |
|---|---|---|---|
| TMU | `8C15D17C` (inicialização dos temporizadores) | TSTR/TCOR/TCNT/TCR | poucas |
| TMU | `8C15D1A2` (leitura do temporizador: `mov #-1,r0; ...; sub r3,r0`) | TCNT0 | 26 990 116 |
| TMU | `8C18BC3C`, `8C1C4C9E`, `8C1C512E`, `8C1C53E2` (medidores do CRI) | TCNT0 | ~4 000 cada |
| INTC | `8C08BD74` (prioridades no boot) | IPRA–IPRD | 1 cada |
| CCN | `8C00FA00` (tratador de exceções do SDK) | INTEVT `FF000028` | 32 062 |
| CCN | `8C010000`, `8C158AB4` | CCR (cache) | poucas |
| CCN | `8C15F078`, `8C160240`, `8C160CD4`... | QACR0/1 (store queues) | centenas de milhares (gráfico: N8.7) |
| cache arrays | `8C158AA2`, `8C17B7CC`, `8C17B9F0`, `8C17BE18` | `F4000000` (limpeza de cache) | até 8 192 |
| portas (BSC) | `8C15E86E` | PDTRA `FF800030` (tipo de cabo) | 2 |
| RTC do SH-4, SCIF | — | sem acesso dinâmico | 0 |

## 2. O que o `BS_POLL` ainda faz (e como preservar sem ciclos)

No experimental o instante só anda nos pontos de espera. O poll continua com três papéis:
1. **entrega de interrupção pendente** fora dos pontos de espera (uma IRQ que venceu numa espera mas estava mascarada,
   ou pendente do Maple/GD-ROM);
2. **atendimento do GD-ROM legado** (`gdrom_poll`; some com o N8.3);
3. **contagem de progresso** (`c->polls`) para a guarda de vivacidade.

Direção aprovada (cadência B, seção 6.2): a cadência deixa de ser "a cada 2 000 ciclos estimados"; todo poll consulta
uma bandeira barata de "há nível entregável" e entrega no primeiro ponto seguro (`c->pc` confiável); `BS_CYCLES` vira
nada no runtime (a macro continua definida). O papel 3 já usa `polls`.

**Regra do comparador para o papel 1 (mantenedor, 2026-10-24): o SPC não é relaxado.**
- `(nível, SPC)` de cada entrega é comparado na ordem, por intervalo entre dois VBlank-In, junto com `T/N/R/J`;
- o mesmo executável e o mesmo modo exigem SPC idêntico;
- entre implementações, uma diferença de SPC (a mesma entrega, na mesma posição) só é aceita por **exceção explícita,
  por diferença**: caso, VBlank, SPC da referência e o novo, justificativa e evidência (estado por passo e saves
  iguais, sequência de eventos seguinte igual, os dois SPC na mesma função sem memória compartilhada com o tratador
  entre eles, pelo disassembly). Cada entrada vale para uma única diferença; deslocamento de posição não tem exceção
  no comparador do N8.0 e reprova;
- o `rte` voltar ao SPC é invariante de execução, não prova de equivalência.

Detalhes em `docs/N8-0-REFERENCIAS.md` seção 5. **Prova antes de mexer no gerador:** um modo do runtime com a nova
cadência (sem regenerar) e a conferência contra as referências do N8.0 (`tools/dev/n8_referencias.py conferir`). Só
depois o gerador deixa de emitir `BS_CYCLES`.

## 3. Matriz de migração

| Item | Símbolos | Depende de | Substituto nativo | Risco | Testes |
|---|---|---|---|---|---|
| Cadência do poll | `sh4_service`, `kServiceQuantum`, `next_event` | `c->cycles` | contador de polls | médio | referências N8.0 (passo, leituras, quadros, áudio), `test_assistido`, sessões |
| Ciclos estimados | `BS_CYCLES`, `Sh4Context::cycles` | gerador | nenhum (removidos) | médio (build) | build do gerado, `n0_fechamento` (catraca `tempo.consumidores.ciclos`) |
| Tempo estável | `time_legacy`, `NativeTime` estável | ciclos | **decisão D-N8.1-A abaixo** | alto | sessões antigas, `test_tempo`, `test_n2b` |
| INTC | `sh4_intc.*`, IPR | prioridade/máscara/INTEVT | ponte de eventos (fila nativa → tratador do guest com o mesmo contrato: SPC, SSR, INTEVT) | muito alto | trace de ordem (B1/B2), pausa, LOAD, sessões |
| INTEVT | leitura do `8C00FA00` em `FF000028` | CCN | a ponte grava o código do evento onde o tratador lê, ou slot nativo do tratador | alto | o mesmo |
| TMU | `sh4_tmu`; `8C15D17C`, `8C15D1A2`, 4 medidores | TCNT0 | slots nativos sobre o relógio lógico (D7: parado no passo, monotônico por leitura) | médio | B11, timeout de render (N7 do `test_n2b2`), medidores do CRI |
| CCN não gráfico | CCR, cache arrays | — | operações sem efeito no host (documentadas) | baixo | boot, campanha |
| Portas | PDTRA em `8C15E86E` | BSC | constante do tipo de cabo (o valor que o legado devolve hoje) | baixo | boot, vídeo |
| Pad e DMA do Maple | coleta do `pad_adapter` "depois do servidor" | fim do DMA (tempo) | coleta no ponto de amostra do passo | médio | `test_pd_nativo`, `test_controles` |

## 4. Bloqueios e decisões previsíveis

- **D-N8.1-A — o que vira o `--tempo nativo` (estável) sem ciclos.** Sem ciclos, o estável não tem como medir a
  duração do trabalho: na prática vira o tempo lógico. Opções: (a) `--tempo nativo` passa a ser sinônimo do lógico e
  as sessões antigas reproduzem com a exceção `d11-carga-cartao` (1 passo por LOAD, saves iguais); (b) o estável por
  ciclos vai para o alvo de validação (D-N8-3) e reproduz as sessões antigas lá. Recomendação: **(a) no produto e (b) no
  validation**. O plano cita "15 411 passos estáveis sem diferença" como evidência: com (a), esse número passa a valer
  no validation, e no produto vale "por passo igual, fora da lista fechada".
- **D-N8.1-B — padrão do headless.** As baterias históricas (`test_tempo`, `test_n2b`, `test_cri`, `test_efeito`) medem o
  modelo por ciclos. Ou vão para o validation com o estável antigo, ou ganham referências novas. Recomendação: validation.
- **Ponte de interrupções** (L5): o contrato do tratador do SDK (`8C00FA00`: INTEVT, SPC, SSR, `rte`) tem de ser
  reproduzido sem a INTC; é o item de maior risco do N8.1 e merece revisão independente própria.
- **Medidores do CRI:** leem o TCNT0 direto (não pela função de leitura); precisam de slot ou de uma leitura do relógio
  na fronteira de memória (o que ainda é "registrador" no substrato AOT; decidir pela forma mais estreita).
- Nenhum bloqueio de N7: o N8.1 não toca o render.

### 4.1 Diretrizes aprovadas (mantenedor, 2026-10-24)

Valem só depois do N8.0 fechado e condicionadas a provas de equivalência contra as referências:
1. **Cadência B:** bandeira barata de IRQ entregável e entrega no primeiro ponto seguro, preservando máscaras,
   prioridades, ordem, `SR.IMASK/BL` e SPC.
2. **Ponte:** reproduzir o contrato observável do tratador `8C00FA00` (INTEVT, SPC, SSR, `rte`), sem reescrever a lógica
   do SDK.
3. **Medidores do CRI:** um slot nativo para cada um.
4. **D-N8.1-A:** as duas fases da seção 6.3 estão aprovadas, mas `--tempo nativo` **não muda de significado** agora; as
   gravações antigas continuam obrigatórias.
5. **D-N8.1-B:** as baterias históricas por ciclos só vão para o validation no N8.2, depois de implementado e provado;
   nenhuma referência sai antes.
6. As macros do gerador ficam; a validação é no runtime; gerador, Builder e cache só mudam depois da equivalência
   provada.

Conflito com as referências: interromper a alteração, registrar a evidência e propor a correção; nunca forçar a
equivalência.

**Do A/B de áudio do N8.0 (2026-10-25), para o N8.1:**
- o índice de uma thread depende da ordem de criação: no experimental com o áudio nativo a thread do servidor do CRI
  (`8C158840`) é criada antes e vira a 1, não a 3. Entre implementações, a identidade da thread é a entrada (`N`);
  no mesmo executável e modo o índice é estável, e o comparador do N8.0 compara `T/N/J` por índice;
- com o TMU parado (diagnóstico) e o áudio nativo, a hipótese D7 não vale em dois pontos: no `test_n2b` B11 uma IRQ
  `0x320` antes da `0x360` no VBlank 2114 e no `test_efeito` E6 uma troca a mais para a thread do CRI no VBlank 2095.
  Fica para os medidores do CRI e o TMU deste N8.1.
- fixação aprovada (opção b, 2026-10-25): `test_cri` e `test_efeito` rodam inteiros com o áudio legado. A
  equivalência por passada do bloqueio do CRI (C3/E3) com o **áudio nativo** é requisito da validação dos slots do CRI
  neste N8.1, não herdada dessas suítes. As propostas S2n e S4n (testes nativos, `STATUS.md`, N8.0) aguardam o
  mantenedor.

## 5. Ferramentas prontas para o N8.1

- `tools/dev/n8_referencias.py` (`capturar`/`conferir`): estado por passo, leituras, quadros por passo, vozes, efeitos,
  legendas, operações do cartão e saves, por roteiro e sessão, nos dois modos.
- `tools/dev/n2_sessoes.py` (lista fechada `config/n2_excecoes.toml`), `test_assistido`, `test_n2b2` (negativos de
  vivacidade).
- Manifesto de hardware (`analysis/hwscan*`) para a catraca: TMU, INTC, CCN não gráfico e portas têm de ir a zero no
  produto.

## 6. Relatório técnico (2026-10-24, somente leitura)

Complementa as seções 1–5 e o inventário L1–L22 (`docs/N8-PLANO.md`). As decisões da seção 6.7 foram tomadas em
2026-10-24 (seção 4.1). Os critérios de aceitação das 16 referências ficaram em `docs/N8-0-REFERENCIAS.md`.

### 6.1 Contrato atual de interrupções (geração → fila → entrega)

**Geração.** Os eventos que viram interrupção nascem no provedor de tempo (`cpu/time_hooks.h`):
- VBlank-In/Out pela linha do SPG; fim de render (STARTRENDER); fim do DMA do Maple; bloco do GD-ROM;
- `NativeTime` guarda-os em filas nativas (`display_`, `disc_`, `native/platform/event_queue`); o legado calcula por
  ciclos (`time_legacy.cpp`).

**Enfileiramento no "hardware".** `holly_intc::update(now)` pergunta `time_provider().holly_due(now)` e acende os bits do
`ISTNRM` (VBlank 3/4, render 0–2, Maple 12); `holly_intc_raise` acende fontes síncronas. É o contrato que o tratador do
jogo lê: o tratador do SDK lê e limpa `ISTNRM` (0x005F6900, escrever 1 limpa) e as máscaras `IML2/4/6*`.

**Entrega à CPU** (`sh4_intc.cpp`):
1. `deliverOne`: se `SR.BL = 0` e o nível IRL do Holly (`holly_irl_level`: bits de `ISTNRM/EXT/ERR` & `IML6/4/2`) é
   maior que `SR.IMASK`;
2. `take`: grava `INTEVT` (0x320/0x360/0x3A0), `SSR = SR`, `SPC = c->pc`, `SGR = R15`, `SR |= MD|RB|BL` e chama o
   tratador `VBR + 0x600` (`8C00FA00`) **aninhado** (`sh4_dispatch`); o `rte` do tratador volta ao SPC
   (`ret_expect`); trocas de thread suspendem a fibra (`sh4_threads`);
3. `sh4_irq_check` repete até não haver nível pendente (uma fonte ainda acesa depois do `rte`).

**Onde a entrega pode acontecer** (só onde o gerado deixou `c->pc` = ponto de retomada):
- `BS_POLL` (2 272 back-edges) e `BS_POLL_RET` (14 987 retornos de chamada): `sh4_service` quando `cycles >=
  next_event` (a cada 2 000 ciclos estimados);
- `BS_IRQ_CHECK` (2 610, depois de `ldc ...,sr`): entrega o que o SR acabou de desmascarar;
- pontos de espera (`BS_WAIT`, 15): no experimental o instante anda e `next_event = 0` força o atendimento.

**Dependências por mecanismo:**

| Comportamento | `BS_CYCLES` | `BS_POLL` | `sh4_service` | INTC |
|---|---|---|---|---|
| relógio do estável e do legado (`time_now = ciclos`) | sim | — | — | — |
| cadência dos atendimentos | sim (`next_event`) | sim | sim | — |
| eventos do Holly vencidos (`holly_intc_sync`) | estável: sim; experimental: não (T) | sim | sim | — |
| fonte que fica entregável por escrita nas máscaras do Holly (`IML*`) ou limpeza de `ISTNRM` | — | **sim**: só no próximo poll | sim | sim |
| fonte desmascarada pelo SR (`ldc sr`) | — | — | — (`BS_IRQ_CHECK`) | sim |
| entrega de uma IRQ (SPC, SSR, INTEVT, tratador) | — | ponto | — | **sim** |
| progresso para a guarda de vivacidade | — | `++polls` | `progress()` | — |
| GD-ROM legado (`gdrom_poll`) | — | sim | sim | — |
| preempção da thread 3 no estável | sim (trabalho) | sim | sim | sim |

O detalhe que restringe as alternativas: uma interrupção **só pode ser entregue onde `c->pc` é conhecido**. No AOT o
`pc` não é atualizado por instrução; entregar dentro de um tratador de escrita (p.ex. quando o jogo desmascara
`IML6NRM`) daria um SPC errado e o `rte` não voltaria ao lugar certo.

### 6.2 Alternativas ao contador de ciclos (entrega determinística)

| | A. Cadência por polls | B. Entrega a cada poll (K = 1) com bandeira | C. Só nas esperas e no `BS_IRQ_CHECK` |
|---|---|---|---|
| Ideia | `next_event` vira "a cada K polls" (contador estrutural `c->polls`) | todo poll consulta uma bandeira "há nível entregável"; só então `sh4_service` | `BS_POLL` vira só `++polls`; a entrega fica nos pontos de espera, no `ldc sr` e depois do `rte` |
| Equivalência ao experimental atual | **a provar**: pode mudar o SPC e a posição da entrega; toda diferença de `(nível, SPC)` ou de ordem por VBlank reprova, salvo exceção explícita e comprovada (regra de 2026-10-24) | **a provar**, com a mesma regra; a entrega mais cedo muda o SPC e precisa de exceção comprovada por diferença | **a medir**: uma fonte desmascarada pelas máscaras do Holly só seria entregue na próxima espera, talvez depois de lógica do passo |
| Determinismo | sim (polls são estruturais) | sim | sim |
| Custo de execução | ~igual ao atual | baixo se a bandeira for barata (um teste por poll) | o menor |
| Impacto no gerado | nenhum (só o runtime) | nenhum | nenhum |
| Risco | baixo; escolher K (o quantum atual ≈ 2 000 ciclos ≈ algumas dezenas de polls) | baixo; a bandeira tem de ser mantida em todo lugar que muda nível/máscara (Holly, SR) | médio/alto: depende de nenhuma espera de lógica entre o desmascarar e a próxima espera |

**Recomendação: B** (bandeira barata + entrega no primeiro poll em que há nível entregável), com **A** como recuo se a
bandeira for difícil de manter correta. C só com prova (referências iguais), porque muda a ordem relativa entre o
tratador e a lógica do passo.

**Em todas:** `BS_CYCLES` vira **nada no runtime** (a macro continua definida), e o gerador deixa de emiti-la só depois.
Motivo: o Builder embute o gerador (`recomp_run` em `bs_builder_core`) e os projetos editáveis trazem o próprio C++
gerado (talvez editado): eles precisam continuar compilando. `BS_POLL` continua existindo como ponto de entrega e de
progresso, sem ciclos.

**Prova antes de qualquer mudança no gerador:** um modo do runtime com a nova cadência (sem regenerar), conferido contra
as referências do N8.0 nos dois modos (`n8_referencias.py conferir`), mais `test_n2b2` (vivacidade, negativos) e as
sessões (`n2_sessoes.py`).

### 6.3 Compatibilidade temporal: D-N8.1-A e D-N8.1-B (análise; decisão na seção 4.1)

Fato: sem ciclos, o estável não tem como medir a duração do trabalho da CPU (é exatamente o que o distingue do lógico).

| Opção | O que é | Prós | Contras |
|---|---|---|---|
| **A1** manter `--tempo nativo` como está (por ciclos) **no produto** | nada muda | gravações e baterias antigas iguais | contradiz o N8.1 (ciclos no produto); impede fechar o N2c |
| **A2** `--tempo nativo` = lógico no produto (sinônimo de `experimental`) | só o lógico no produto | produto sem ciclos; CLI compatível | sessões antigas reproduzem com a lista fechada (1 passo por LOAD; saves iguais); `gravacoes.py reproduzir` (igualdade estrita) precisaria da regra da lista fechada |
| **A3** estável por ciclos só no **alvo de validação** (D-N8-3), produto com o lógico | referências históricas preservadas onde foram medidas | as baterias por ciclos continuam válidas lá | alvo de validação a manter; separação real das bibliotecas depende do N8.2 (ordem do plano) |

| Opção | Headless padrão | Efeito nas baterias |
|---|---|---|
| **B1** headless continua no estável por ciclos | só possível com A1 ou no validation | `test_tempo`, `test_n2b`, `test_cri`, `test_efeito` iguais |
| **B2** headless no lógico; baterias históricas no validation | — | baterias rodam no validation contra as referências delas; o produto é conferido por `n8_referencias.py` e `n2_sessoes.py` |
| **B3** headless no lógico; baterias com referências novas | — | trabalho de regravar referências; perde a comparação histórica |

**Recomendação (para decisão):** A2 + A3 e B2. Ponto de ordem: o validation separado é do N8.2, depois do N8.1. Para não
inverter a ordem do plano, o N8.1 pode fazer a remoção em dois passos: (1) o produto passa ao lógico com o estável por
ciclos ainda selecionável (`--tempo nativo` intacto até a aprovação); (2) com o N8.2, o estável vai para o validation.
Assim a semântica de `--tempo nativo` não muda sem a sua aprovação.

### 6.4 Matriz de testes do N8.1

| Área | Testes existentes reutilizáveis | O que provar | Novo? |
|---|---|---|---|
| Interrupções | `test_n2b2` N5 (padrões de eventos), N8 (vivacidade, negativos), `test_tempo` A1/A2 (no estável), traces `BS_EVENT_TRACE` | ordem por passo; nenhuma IRQ perdida; simultâneos | trace de entrega por passo (INTEVT, tarefa) comparado entre a cadência antiga e a nova |
| Timers (TMU) | `test_n2b2` N7 (timeout de render), B11, medidores do CRI | `8C15D1A2` e os 4 medidores com a semântica D7; timeout de render sai | negativo: TMU congelado de vez → o timeout tem de falhar |
| LOAD | `n2_sessoes.py` (lista fechada), `passo_load_slot1` e as sessões em `n8_referencias.py` | 1 passo por LOAD no máximo, saves iguais | — |
| Determinismo | `test_n2b2` N10, duas execuções do `conferir` | artefatos idênticos entre duas execuções | — |
| Eventos de jogo | `n8_referencias.py` (estado por passo, leituras, quadros por passo) | igual por passo nas rotas e sessões | — |
| Áudio | vozes e efeitos no `n8_referencias.py`; H9 na janela (`h9_var`) | sequências iguais; 0 faltas | — |
| Saves | `test_saves`, saves no `conferir`, VMS | hashes iguais | — |
| Gravações antigas | `n2_sessoes.py` nos dois modos; `gravacoes.py reproduzir` | conforme D-N8.1-A | regra da lista fechada no `reproduzir` se A2 |
| Pausa/menu/cursor/câmera | `n7_menu_pausa.py`, `n7_cursor.py`, `n7_camera_transicao.py` | sem regressão | — |
| Produto | catraca `tempo.consumidores.ciclos` (`n0_fechamento`), `bs-hwscan` | ciclos e INTC/TMU fora do produto | catraca nova para INTC/TMU/CCN não gráfico |

### 6.5 Segurança da migração

**Remover juntos** (cada grupo num commit, conferido sozinho):
1. cadência sem ciclos (`sh4_service`, `next_event`) + `BS_CYCLES` sem efeito: nada mais lê ciclos para entrega;
2. TMU: `sh4_tmu` + slots nativos de `8C15D17C`, `8C15D1A2` e dos 4 medidores do CRI (senão o jogo lê um registrador
   que não existe);
3. INTC: `sh4_intc` + a ponte de eventos (o contrato do tratador `8C00FA00`: INTEVT, SPC, SSR, `rte`) + a leitura de
   INTEVT (CCN `FF000028`);
4. CCN não gráfico (CCR, arrays de cache) + portas (`PDTRA`): operações sem efeito e constante do cabo.

**Ficam para depois:**
- `holly_intc` (os bits de `ISTNRM` são o contrato que o tratador do jogo lê): só a parte de tempo sai no N8.1; o
  módulo sai no N8.7;
- `sh4_dmac`, `sh4_sq`, QACR do CCN, TA/VRAM/PVR: N8.7;
- GD-ROM e `gdrom_poll`: N8.3; Maple e o fim de DMA do Maple: N8.4 (a coleta do `pad_adapter` presa ao DMA do Maple sai no
  N8.1 só se houver ponto de amostra nativo provado);
- `time_legacy`/estável por ciclos: conforme D-N8.1-A.

**Pontos de reversão:** um commit por grupo; `--tempo nativo` intacto até a decisão; a Preview `5ce94b5` preservada
(`dist\BlueStinger-Preview.n8-0-5ce94b5`); as referências do N8.0.

**Critérios objetivos para interromper uma mudança:**
- qualquer diferença por passo fora da lista fechada em `conferir`/`n2_sessoes.py`;
- qualquer save diferente;
- disparo da guarda de vivacidade, deadlock ou "evento vencido não entregue";
- duas execuções diferentes (determinismo);
- desempenho do lógico pior que o atual além de 5% (B9);
- uma falta de áudio a mais no H9.

### 6.6 Builder e distribuição (inspeção)

- O Builder **embute o gerador** (`recomp_run`, `bs_builder_core`) e regenera `generated/` do `1ST_READ.BIN` do
  usuário: mudar o gerador exige Builder novo e invalida o cache do C++ gerado (`%LOCALAPPDATA%\...\build`).
- Projetos editáveis compilam o **próprio** `generated/`: macros removidas quebrariam projetos existentes. Por isso as
  macros continuam definidas (sem efeito) no N8.1.
- `kKitEntries` leva `runtime`, `transitional`, `legacy`, `native`, `app`; `check_kit` exige os `CMakeLists.txt` de
  `runtime` e `legacy`. Remover arquivos de `runtime/` exige atualizar `runtime/CMakeLists.txt` e o kit no mesmo commit
  (`make_builder.ps1` lista o kit pelo git).
- `legacy/` hoje é só `bs_legacy INTERFACE → bs_runtime` (L22): a separação real é do N8.2. No N8.1, remover do produto
  significa tirar do caminho de execução (e da catraca), não ainda do alvo `bs_runtime`.
- `test_builder`, `test_projeto` e `test_preview` cobrem kit, projeto e pacote; rodar os três depois do N8.1.

### 6.7 Decisões que eram pendentes (tomadas em 2026-10-24, seção 4.1)

1. **D-N8.1-A** (semântica de `--tempo nativo`) e **D-N8.1-B** (headless e baterias históricas): seção 6.3.
2. **Estratégia de cadência** (A, B ou C da seção 6.2): recomendação B, com prova contra as referências.
3. **Forma da ponte de interrupções:** reproduzir o contrato do tratador `8C00FA00` (INTEVT, SPC, SSR) no runtime,
   ou um slot nativo do tratador do SDK (maior desacoplamento, maior risco; mais perto do N10). Recomendação: reproduzir
   o contrato no N8.1.
4. **Medidores do CRI** que leem o TCNT0 direto: slot de cada um ou leitura na fronteira de memória. Recomendação: slot
   (fronteira estreita, catraca simples).

## 7. Andamento (execução do N8.1)

Autorização do mantenedor em 2026-10-25: só o N8.1; `--tempo nativo`, o padrão do headless, as macros do gerador e
as referências do N8.0 não mudam.

### 7.1 S2n e S4n (referência pré-migração)

`tools/dev/n8_nativo.py` (29 testes sem o jogo) e `tools/dist/test_n8_nativo.ps1`, com o áudio nativo:
- **S2n**, espera do VBlank `8C064A2C` pelo trace de progresso:
  - cada episódio fechado pelo VBlank-In;
  - 0 saídas sem evento ou liberadas por outro evento;
  - no máximo um episódio aberto, e só se a execução termina dentro dele;
  - trace e execução completos.
  
  Roda nos dois modos de tempo e em duas rotas, com negativo (`sem_espera`) e determinismo.
- **S4n**, servidor do CRI pelo `BS_CALL_TRACE`:
  - tempo legado = modo sombra linha a linha, 2 008 064 chamadas;
  - negativo `menos1` com trace completo e menos chamadas;
  - a sequência do experimental fica como referência.
- **Referência** em `analysis/n8/pre-n8-1` (local): 17/17 na captura e depois de cada mudança.

### 7.2 Instrumentação observacional (`BS_INTC_TRACE`)

`runtime/debug/intc_trace.*`, num fluxo separado, no eixo de ordem do `BS_EVENT_TRACE` (`pos`):

| Tipo | Registra |
|---|---|
| H | bits do `ISTNRM` acesos (`update`/`raise`), com a origem do atendimento |
| A | bits limpos |
| C | leituras de relógio, agrupadas |
| L | operações do cartão |
| E | origem de cada entrega |
| S | atendimento da cadência com efeito |

Resumo por `tools/dev/n8_intc.py`.

**Prova de que não muda nada:** as 16 referências do N8.0 iguais com o trace desligado e ligado.

**Medida, nos 8 casos experimentais:** 0 entregas e 0 efeitos pelo quantum de ciclos; toda entrega vem de atendimento
forçado (espera que andou o instante, DMA), de `ldc sr` ou da espera do hardware. No estável, o quantum responde por
14 000–39 000 entregas por caso.

### 7.3 Cadência B (D-N8.1-A preservada)

**Tempo lógico (`--tempo experimental`):**
- `sh4_service` só roda quando pedido:
  - `next_event = 0` pela espera que andou o instante e pelo fim do DMA ch2 (como antes);
  - `sh4_irq_wakeup` quando `holly_intc_raise` ou uma escrita de `IML*` deixa um nível entregável (`SR.BL = 0`,
    nível > `SR.IMASK`, sem avaliar eventos);
  - `wakeIfDue` para evento agendado no instante atual.
- A vivacidade (D8) vai por um tick a cada `kProgressTick` = 4 096 polls (`next_poll`, macro `BS_POLL_KIND`), que só
  chama `progress()`.

**Tempo estável e legado:** o quantum de ciclos continua (`cycle_cadence()`). O contexto da sessão é registrado no boot
e zerado no reset.

**Correção aprovada (`timed_wait`, espera do hardware no experimental):**
- o `c->pc` é restaurado para o retorno da espera antes de cada atendimento;
- antes, o SPC das entregas seguintes era um `c->pc` velho de código aninhado;
- o retorno é o endereço depois do `jsr` do download da música: `8C15DE72` no boot, `8C02A582` nas trocas.

**Resultado contra as 16 referências do N8.0:**
- estável: 8/8 idêntico;
- experimental: idêntico fora de 179 SPCs, todos nessas esperas, em `config/n8_1_excecoes_spc.toml`.

Cada uma das 179 exceções é conferida por `tools/dev/n8_excecoes_cadencia.py` (7 testes negativos sem o jogo), que
aborta em qualquer outra diferença:
1. só o SPC muda, na mesma posição e com o mesmo INTEVT, e o par de SPCs é um dos provados pelo disassembly;
2. a primeira entrega da mesma espera já tinha o SPC novo na referência. Entre as duas não há `T`, `J` nem `N`, e toda
   entrega no caminho tem origem `hardware` no `BS_INTC_TRACE` da execução nova (revisão Codex);
3. os eventos seguintes até a próxima I/V são iguais (81 com o mesmo `rte` desviado para `8C17DE4C`, 98 sem nenhum),
   sem troca de thread nem longjmp;
4. o primeiro estado gravado depois é igual.

O gerador não compara o caso inteiro: isso é do `n8_referencias.py`, com a lista gerada.

O SPC só passa pelo tratador `8C1982F4` em `stc.l spc` (`8C19831C`) e `ldc.l spc` (`8C19845A`), antes do `rte`
(`8C198462`).

**Prova de independência dos ciclos:** `BS_CICLOS_RUIDO=5000` (`runtime/debug/cycle_noise.cpp`) soma ruído ao contador
a cada atendimento e a cada tick:
- experimental: 8/8 iguais, com as mesmas 179 exceções;
- estável: 0/8 iguais (controle: o ruído muda o tempo dele).

### 7.4 TMU no tempo lógico (interceptação MMIO, aprovada em 2026-10-25)

**Levantamento** (manifesto `bs-hwscan` estático e sonda dinâmica):
- 6 leituras do `TCNT0` no código; 5 alcançadas:
  - `8C15D1A2` devolve `0xFFFFFFFF − TCNT0`: 27 M leituras na rota;
  - os 4 medidores do CRI marcam instantes dentro de funções grandes da biblioteca: início em `8C18BCAA` e `8C1C4DE6`,
    fim em `8C1C51D2` e `8C1C5438`, que gravam `início − TCNT0`.
- O canal 0 é configurado uma vez no boot por `8C15D17C`: P/64, `TCOR0 = TCNT0 = 0xFFFFFFFF`, partida.
- Os canais 1 e 2 (`8C15D300`, `TCR2`) nunca são alcançados.

**Opções:**

| Opção | Por que não |
|---|---|
| slot nativo para `8C15D1A2` | exige `[native] slots`, regenerar e mudar a configuração embutida no Builder |
| leitura tipada emitida pelo gerador | muda o gerador |
| reimplementar o CRI | vetado pelo mantenedor |

**Escolha: interceptação MMIO** (`runtime/memory/tmu_logical.*`):
- no tempo lógico, o serviço atende o contrato do boot sobre o instante lógico, com a mesma semântica do modelo (base,
  rebase em TSTR e TCR, UNF);
- fora do contrato (outro canal, divisor ou registrador; espelho da área 7), o estado vai para o `sh4_tmu`
  (`sh4_tmu_adopt`) e o acesso é contado;
- o detector de ociosidade da thread 3 inclui o estado do serviço;
- mutação `BS_TMU_MUTACAO=parado`.

**Prova:**
- as 16 referências iguais com as 179 exceções e nenhuma nova;
- as leituras do TMU iguais em todos os 16 casos (registros C);
- 0 fora do contrato; ruído de ciclos 8/8;
- negativo: com o TMU parado, o timeout de 166 ms do render não vence.

**Limitação:** a instrução de leitura continua no código do guest. Sai com o gerador, no N8.2 ou depois.

### 7.5 CCN não gráfico e porta do cabo

O mesmo padrão, em `runtime/memory/sys_logical.*`:
- **CCR:** ICI e OCI sempre leem 0.
- **Arrays do cache:** leitura 0 e escrita sem efeito, só 32 bits. Pelo disassembly (`8C17B830`–`8C17B842`), o valor
  lido só forma o endereço do `ocbi` seguinte, sem efeito no host, e a saída do laço não depende dele.
- **PDTRA:** o cabo nos pinos 8–9, com o `PCTRA` em 0.
- **Fora do contrato:** vai para o `sh4_ccn`/`sh4_port` (`*_adopt`).
- **Fica para o N8.7:** QACR (store queues) e DMAC.

**Prova:** as 16 iguais com as 179; 1 036 acessos e 0 fora do contrato em cada caso experimental; ruído 8/8.

### 7.6 Ponte de interrupções (contrato de `8C00FA00`)

`runtime/cpu/irq_bridge.*`. No tempo lógico, `sh4_irq_check` (a entrada do código gerado) desvia para a ponte, que
repete o contrato:
- `SR.BL`/`IMASK` × nível IRL do Holly;
- `INTEVT`, `SSR`/`SPC`/`SGR`, `SR |= MD|RB|BL`;
- tratador aninhado com o `rte` conferido e a reavaliação depois do `rte`;
- os mesmos registros E e I.

A ponte atende o `INTEVT` e o `ICR`/`IPR` do boot (escritos uma vez, nunca lidos).
- **Fora do contrato:** o estado vai para o `sh4_intc` (`sh4_intc_adopt`). Dentro de um tratador, o modelo reavalia e
  entrega o resto (revisão Codex).
- **Contador:** `sh4_irq_taken_count` soma as entregas da ponte.

**Prova:**
- as 16 referências iguais com as 179;
- o `BS_INTC_TRACE` inteiro igual ao do build anterior em todos os 16 casos;
- as entregas da ponte iguais às entregas do N8.0 em cada caso experimental;
- 0 fora do contrato; ruído 8/8.

### 7.7 Encerramento e limites

**No tempo lógico** (padrão da janela e do produto), o caminho de execução:
- não depende dos ciclos estimados: o ruído de ciclos não muda nada nas 8 referências experimentais;
- não alcança o modelo do TMU, do INTC, do CCN não gráfico nem da porta: 0 acessos fora do contrato em todas as rotas.

**Ficam, por decisão do mantenedor (D-N8.1-A e D-N8.1-B):**
- o `--tempo nativo` estável por ciclos, o padrão do headless e as gravações antigas;
- os modelos `sh4_intc`, `sh4_tmu`, `sh4_ccn`, `sh4_port` e `time_legacy`, que servem o estável e são o recuo
  quando o jogo sai do contrato;
- a remoção física e a separação do alvo de validação ficam para o N8.2;
- as macros `BS_CYCLES`/`BS_POLL` continuam definidas; `BS_POLL` é ponto de entrega e de vivacidade. O gerador só
  muda depois da equivalência provada.

**Pendência herdada do N8.0:** B11/E6 (hipótese D7 com o TMU parado e o áudio nativo). Só existe no modo de diagnóstico
do estável (`BS_TMU_DIAG`), que o N8.1 não mudou; fica registrada para o N8.2, junto com o alvo de validação.
