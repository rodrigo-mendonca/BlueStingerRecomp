# N8.1 (N2c) — preparação técnica

**Situação (2026-10-25): levantamento somente leitura, durante o N8.0. Nada implementado.** O N8.1 começa depois da
aprovação do N8.0 e de autorização explícita. Referências: `docs/N8-PLANO.md` (L3–L8 e a seção 5), `docs/N2-PLANO.md`
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
