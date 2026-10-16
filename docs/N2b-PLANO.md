# N2b: tarefas, pontos de espera e tempo nativo (plano técnico)

Data: 2026-10-15. Base: N2a (commit `fdf326a`). Autorização do mantenedor: **só investigação,
planejamento e testes diagnósticos**, sem mudança estrutural de agendamento e sem mexer na
arquitetura de gameplay.

Plano-mestre: `docs/N2-PLANO.md` (D1–D7) e `docs/PLANO-PORT-NATIVO.md`, seção 5. Revisão do
Codex: seção 12.

## 1. O que foi investigado (evidências)

Método: só diagnóstico, no roteiro NEW GAME com relógio fixo.
- Disassembly (`bs-disasm`).
- Trace de chamadas (`BS_CALL_TRACE`). Ganhou o ponto exato da chamada (PR) e o VBlank;
  mudança só de diagnóstico.
- Trace de ordem (`BS_EVENT_TRACE`).
- Busca de literais no executável.
- Novo diagnóstico `BS_TMU_DIAG=quadro`.

### 1.1 Modelo de execução real: dois níveis de agendamento

**Nível 1: threads do SDK, preemptivas na volta da interrupção.**
- O tratador de interrupção termina num `rte` para `8C17DE4C`, um despachante que salva o
  contexto completo (R0–R14, bancos, SPC/SSR, GBR, MACH/MACL, PR, FPSCR, FPUL) e retoma outra
  thread.
- Threads:
  - **0:** principal;
  - **3:** criada em `8C158840`. Laço infinito: se `[0x8C1589B0] == 1` chama uma rotina por
    ponteiro; senão roda o servidor do CRI (`8C1586B8`).
- As duas **se alternam a cada VBlank** (trace de 3 000 VBlanks: 0→3 e 3→0 1 488 vezes cada).

**Nível 2: tarefas cooperativas do jogo**, em código do jogo recompilado (`8C0903C0`):
- Cada tarefa tem uma estrutura de `0xBC` bytes e um prazo em `+20`; a quantidade fica em
  `+0x138` e o "tempo" em `+0x134`.
- Uma passada do escalonador roda cada tarefa cujo prazo ≤ tempo (`longjmp` para o contexto
  salvo) e faz `tempo++` no fim.
- As tarefas cedem com `8C09037E` (dormir N passos; N < 0 = para sempre) ou `8C0903BC`
  (dormir 1).
- Há 2 tarefas (`8C09081A`, `8C09081E`, threads de host 1 e 2).
- Medido em 8 000 VBlanks: **uma passada por VBlank** (7 976) e 15 950 yields de 1 passo.

**Conclusão:** o agendamento lógico do jogo **já é cooperativo e determinístico, e é código do
jogo**. O N2b não o substitui (arquitetura de gameplay). Nem a escolha entre as threads do SDK
muda: quem decide é o despachante recompilado (`8C17DE4C` → `rte` em `8C17DFB6`). O N2b decide
só **quando o tempo anda**; as fibras continuam dando as pilhas das threads.

**A thread 3 não tem ponto de espera.** O laço de `8C158848`..`8C158862` chama o servidor do
CRI (ou a rotina por ponteiro) repetidamente até uma interrupção preemptá-la. Transformar o
back-edge em espera mudaria quantas vezes o servidor roda por VBlank (revisão do Codex, ALTA).
É o problema central do N2b (seção 3.4).

### 1.2 Pontos de espera (medidos)

PC onde cada thread estava quando a interrupção chegou, em 3 000 VBlanks:

| Ponto | Thread | Vezes | O que espera | Quem acorda |
|---|---|---|---|---|
| `8C064A26` (`8C064A20`, jogo) | 0 | 2 831 | bandeira de VBlank em `0x8C24F88A` (laço `mov.w`/`tst`/`bt`) | tratador do VBlank-In |
| `8C173044` → `8C17306A`..`8C173086` | 0 | 1 por render (3 986 em 8 000 VBlanks) | fim do render anterior (`[0x8C2D9E3C]` = 0), com timeout | fim de render (`8C1725E6`, de `8C1C570C`) |
| `8C1C1FD4` (desmascaramento no SDK) | 0 | 3 047 | interrupção pendente ao baixar a máscara | qualquer IRQ |
| `8C157FC0` (desmascaramento no SDK) | 3 | 2 842 | idem, no laço da thread 3 | qualquer IRQ |
| `8C18BCF4` (depois de `pref`) | 0 | 1 486 | fim de lista do TA (síncrono) | TA |
| `8C15D1DA`, `8C15870E` (CRI) | 3 | 163 | trabalho do servidor do CRI (não é espera) | — |
| laço `8C158848`..`8C158862` | 3 | — | **não é espera**: repete o servidor até a preempção | IRQ + despachante |
| `8C0978BE` | — | 0 | bit de estado com timeout (não exercitado) | — |
| `8C15BC26` (gdFsStop) | — | 0 no nativo | drive parar, timeout (−21) | substituído pelo provedor nativo de arquivos (N1) |

Pendências do N2b-0:
- confirmar, por trace de escrita, que a bandeira `0x8C24F88A` vem do VBlank;
- identificar a rotina que a thread 3 chama quando `[0x8C1589B0] == 1`;
- medir, por VBlank, quantas vezes a thread 3 chama o servidor `8C1586B8` e a rotina por
  ponteiro, e em que ordem: é a referência que o N2b tem de preservar.

### 1.3 A espera com timeout em `8C173044`

Disassembly de `8C173044`..`8C1730A6`:
1. `t0 = syTmrGetCount()`, guardado em `0x8C2DA480`.
2. Se `[0x8C2D9E44]` (habilitado): enquanto `[0x8C2D9E3C] ≠ 0` (render ocupado) e
   `µs(t0, agora) < 0x28870` (166 000 µs ≈ 10 campos), relê o TMU.
3. Se não estourou: chama `8C172FE6`, que executa um gancho opcional (`[0x8C173040]`).
4. Marca o render como ocupado (`[0x8C2D9E3C] = 1`) e segue para o próximo envio.

Quem apaga a bandeira:
- `8C1725E6`: zera a bandeira e liga a habilitação. É chamado no fim de render pela rotina
  `8C1C56xx` (trace: 3 986 vezes, retorno `8C1C570C`);
- `8C158A82`, `8C172A7C` e `8C17286A` (inicializações).

Medido em 8 000 VBlanks:
- 3 986 chamadas, uma por render, de dentro de `8C064A20` (retorno `8C064A5A`);
- `8C172FE6` foi chamado nas 3 986: **o timeout nunca estourou**;
- ~42 voltas do laço por render, em média (165 601 leituras do TMU em 2 400 VBlanks).

Classificação: **espera do fim do render anterior, com timeout de segurança de 166 ms**. No
N2b vira ponto de espera:
- bloqueia até `RenderDone` **ou** até um temporizador nativo de 166 ms lógicos
  (`schedule_timer`);
- o caminho tomado (fim ou estouro) é comparado com o legado (B5).

### 1.4 Consumidores do TMU: classificação preliminar (D7, condicional)

Pontos exatos de chamada (PR), 2 400 VBlanks:

| Chamador (família) | Chamadas | Uso | Classe | O resultado alimenta decisão? |
|---|---|---|---|---|
| `8C1586B8` (CRI, servidor) | 5 laços × 800 mil | mede a duração de cada servidor; acumula em `0x8C1DD1E8`..`F0` | medição | não encontrado: os globais só aparecem no pool do próprio medidor |
| `8C173044` | 165 mil | timeout da espera de render (1.3) | **timeout** | só se estourar (nunca estourou) |
| `8C1799DC` (CRI) | 2 384 × 6 | durações de etapas em `0x8C1DDA7C`.. | medição | idem |
| `8C158A26` (CRI) | 1 por VBlank | carimbo de VBlank (interrupções mascaradas) em `0x8C1DD238` | carimbo | referenciado também por `8C158AB4` (CRI): verificar no N2b-0 |
| `8C158864` (CRI) | 1 por render | carimbos em `0x8C1DD224`..`34` | carimbo | idem |
| `8C08E126` (CRI, vídeo; chamada por registrador) | 496, só no vídeo | início e fim de cada item decodificado | medição | idem |
| `8C1766C0`, `8C1769E6` (sem família) | 70 / 13 | duração de uma operação | medição | globais só no pool local |
| `8C096C5C` (CRI) | 16 | carimbo antes de uma operação | carimbo | idem |
| `8C1583FA` (CRI) | 8 | cronômetro (µs/100 desde uma base) | cronômetro | só no próprio pool |
| `8C0978BE` (sem família) | 0 | espera de bit com timeout | timeout | não exercitado |
| `8C15BC26` (gdFsStop) | 0 no nativo | espera do drive com timeout | timeout | substituído (N1) |
| `8C172E78`, `8C17665A`/`8C17666A` | 0 | `8C172E78` passa o endereço do leitor do TMU como argumento (registro de callback) | registro | não exercitado |
| `8C078400`, `8C08BD00` | 1 cada | `8C15D17C`: liga o canal 0 | inicialização | — |

**Experimento diagnóstico** (`BS_TMU_DIAG=quadro`): o TMU devolve o valor do **instante em que o
último VBlank-In foi entregue** (ponto de poll, não o início exato do campo), aproximando o
tempo parado dentro do passo. Resultado em 8 000 VBlanks:
- **ordem lógica dos eventos idêntica** (tipos, fontes, trocas de thread, `longjmp`); só os
  ciclos e o PC interrompido mudam, porque os laços de medição giram menos;
- **7 750 leituras de arquivo** com o mesmo conteúdo, ordem e VBlank;
- **quadro final idêntico** (`F7818C7E…`).

**Limites dessa evidência** (revisão do Codex): o experimento congela só o valor lido; o avanço
por ciclos e as preempções atuais continuam. Ele aproxima o N2b, mas não o reproduz. Além
disso, buscar o endereço literal não prova que não há leitores por base+deslocamento, ponteiro
ou retorno de função: em `8C158470`..`8C1584A0`, o valor é combinado e gravado por ponteiros.

**Recomendação para a D7 (condicional):**
- **(a) TMU = relógio lógico**, parado dentro do passo e andando quando o tempo anda: o modelo do
  N2b, apoiado pelo experimento. Para provar o congelamento no início lógico do campo, o
  diagnóstico do N2b-0 deve guardar o instante **agendado** do VBlank, não o da entrega.
- Sem avanço por leitura e sem tempo real do host.
- Os timeouts (`8C173044`, `8C0978BE`) viram pontos de espera com temporizador nativo do mesmo
  valor lógico.
- **Hipótese, não decisão**, até o N2b-0:
  - trace de leitura e propagação dos valores temporais (base+deslocamento, ponteiros, retornos)
    dos carimbos do CRI (`0x8C1DD238` e afins);
  - repetir o experimento com o avanço por ciclos desligado (N2b-1, modo sombra) e num roteiro
    com vídeo completo e gameplay.

## 2. Dependências que restam

| Dependência | Situação | No N2b |
|---|---|---|
| Progresso por ciclos estimados (`BS_CYCLES` em 58 590 pontos, `BS_POLL` em 2 287, quantum) | base do tempo | deixa de definir o tempo; vira só guarda de liveness (seção 3.4) |
| Entrega como interrupção (INTC, IRL, VBR+0x600, `rte` para o despachante) | pontos atuais | continua, nos pontos de espera e de desmascaramento (seção 3.3) |
| Threads do SDK em fibras por PC/R15 | mecanismo do host | ficam; a escolha continua só no `rte` do despachante recompilado |
| Escalonador cooperativo do jogo (`setjmp`/`longjmp`) | código do jogo | **sem mudança** |
| TMU, SPG, ISTNRM, RTC como interface do SDK | lidos do relógio lógico | idem (D7); saem no N2c |
| Fim de render, Maple, GD-ROM com custo estimado em ciclos | agendados na fila | instante lógico definido (decisão D9) |
| Eventos síncronos (TA, YUV, DMA do PVR) | na hora | sem mudança |

## 3. Mecanismo proposto

### 3.1 Princípios
- O componente nativo do N2b **não escolhe threads**. Ele só:
  - faz o relógio lógico andar quando o contexto corrente está num ponto de espera (3.4);
  - entrega os eventos vencidos como interrupções.
- A única seleção de fibra do guest continua sendo consequência do `rte` recompilado:
  - o despachante `8C17DE4C` termina em `8C17DFB6: rte`;
  - `sh4_rte_redirect` troca para a fibra cujo SPC/R15 bate.
- A `ITaskScheduler` declarada no N2a (`yield`, `run_ready`) **não é usada** para as threads do
  SDK, porque permitiria uma troca sem IRQ, tratador e `rte`. Proposta (D4 revista): no N2b
  substituí-la por uma interface só de espera e avanço do tempo, ou mantê-la reservada e sem
  implementação.
- **Nada de interpretar instruções ou contar ciclos para decidir.** Os pontos de espera são
  declarados por endereço no config e compilados pelo gerador.
- O código do jogo não muda. O escalonador do jogo continua sendo o código recompilado.

### 3.2 Pontos de espera declarados (`[native] esperas` no config)
- O gerador troca o `BS_POLL` do back-edge daquele laço por `BS_WAIT(pc)`:
  - **a função original fica inteira**: o laço volta a testar a condição depois que a espera
    retorna;
  - `BS_WAIT` entrega os eventos já vencidos e, se o contexto continua parado, deixa o tempo
    andar até o próximo evento (3.4).
- Só entram laços que **esperam uma condição acesa por evento**, com evidência:
  - `8C064A2C` (back-edge da espera do VBlank, bandeira `0x8C24F88A`);
  - `8C173086` (espera de render, com temporizador de 166 ms);
  - `8C0978F6` (timeout de bit; sem exercício).
- **Fora:** o laço da thread 3 e os laços de medição do CRI, que trabalham em vez de esperar.
  A thread 3 só entra com uma condição semântica de ociosidade do CRI (fila vazia, nada a
  decodificar), a encontrar no N2b-0. Sem ela, não há N2b-2 (seção 3.4).
- **Mods de função:** a primitiva é exposta (`bs::mods::wait_event`). Um mod que reescreva uma
  função de espera sem chamá-la cai na guarda, com mensagem que nomeia a função.

### 3.3 Entrega de interrupção
- Pontos mantidos:
  - `BS_IRQ_CHECK` depois de `ldc sr` e de `pref`;
  - os pontos de espera;
  - o fim do tratador (laço de reentrega).
- Os polls dos back-edges continuam, mas **só entregam eventos já vencidos**: não fazem o
  tempo andar.
- Remover um ponto de entrega só com prova por trace (B2).

### 3.4 Progresso do tempo sem travar (revisto depois da revisão do Codex)
1. **Só o contexto corrente roda.** As outras threads do SDK estão suspensas pelo despachante,
   não "aptas" para o N2b. `BS_WAIT` **nunca escolhe outra thread**: só deixa o tempo andar. A
   troca acontece como no original: o evento vira IRQ, o tratador e o despachante rodam (código
   recompilado), e o `rte` do despachante (`8C17DFB6`) retoma quem ele escolher.
2. **O tempo só anda quando o contexto corrente está num ponto de espera declarado** e não há
   evento vencido pendente. Ele vai até o próximo evento da fila. Nunca enquanto o código
   corrente faz trabalho: é a D1.
3. **VBlank sempre agendado.** Fila vazia num ponto de espera é deadlock: erro com o PC e o
   estado das threads.
4. **Guarda de liveness:** um contexto que executa mais que um limite **de pontos de poll** sem
   chegar a um ponto de espera é **erro com o PC, também no produto, durante o N2b**. Um modo
   degradado (o tempo anda pela guarda) só existe por opção explícita, como **não
   equivalente**, para diagnóstico (D8).
5. **Consequência para a thread 3:** sem condição de ociosidade, ela é um laço sem espera, e a
   guarda dispara. O N2b-2 só começa depois que o N2b-0 achar essa condição e o N2b-1 provar que
   bloquear nela preserva a quantidade e a ordem das chamadas ao servidor por VBlank (S4).
6. **Laços de medição do CRI** (dentro do servidor) terminam pela própria lógica com o tempo
   parado (experimento da seção 1.4, ainda com ciclos; confirmar no modo sombra).

### 3.5 Instantes dos eventos que hoje têm custo estimado (D9)
- **VBlank:** a cada campo lógico (base NTSC exata, `TimeBase` padrão).
- **Fim de render, Maple, GD-ROM.** Opções:
  - (a) no próximo ponto de espera (latência lógica 0: o renderizador nativo é síncrono);
  - (b) um intervalo lógico fixo por tipo.

  Recomendação: (a) para Maple e GD-ROM. Para o render, medir as duas no N2b-1, porque o
  jogo espera por ele (1.3).

## 4. Subetapas e critérios

### N2b-0. Referência e instrumentação (diagnóstico, sem mudança de comportamento)
- Trace normalizado por passo lógico: por VBlank lógico, a sequência
  (thread, IRQ, tarefa do jogo, `longjmp`), sem ciclos nem PC interrompido. É a forma que o
  experimento do TMU já mostrou estável.
- Trace de escrita das bandeiras de espera: `0x8C24F88A`, `0x8C2D9E3C`, `[0x8C1589B0]`.
- Snapshots de estado lógico nos pontos de controle do roteiro: sala, posição, vida,
  inventário (a mapear).
- Roteiros de cobertura adicionais: vídeo completo, menu, save/load.

### N2b-1. Pontos de espera e avanço do tempo em modo sombra
- `BS_WAIT` registra onde o tempo andaria e com qual evento; **o relógio ainda anda
  pelos ciclos** (sem mudança de comportamento).
- Critérios:
  - S1: trace de ordem completo **byte a byte** igual ao N2a (o modo sombra não muda nada);
  - S2: cada espera declarada bloquearia, com o evento que de fato a liberou;
  - S3: 0 ativações da guarda nos roteiros, **contando a thread 3** (só passa com a condição de
    ociosidade);
  - S4: chamadas por VBlank ao servidor do CRI e à rotina por ponteiro da thread 3, em
    quantidade e ordem, iguais à referência. Negativo: tirar uma ativação por VBlank tem de
    falhar.

### N2b-2. O tempo deixa de vir dos ciclos

Artefatos:
- **TN** = trace de ordem normalizado. Por VBlank lógico: a sequência de
  `(tipo, INTEVT, thread, função interrompida, destino de longjmp/rte, tarefa do jogo)`, sem
  ciclos nem PC exato;
- **CL** = contrato de leituras (P1);
- **SL** = snapshots de estado lógico;
- **CK** = `BS_CLOCK_LOG` sem o TMU: leituras de SPG_STATUS, MDST, RTC e ISTNRM/EXT/ERR por
  VBlank.

Exceções: só na lista fechada `config/n2b_excecoes.toml`, escrita antes da execução. Cada
entrada traz VBlank, artefato, causa provada e evidência.

| # | Critério | Oráculo (igualdade) | Negativo (tem de falhar) |
|---|---|---|---|
| B1 | Ordem lógica | TN igual à referência do N2b-0, multiplicidade inclusa | ponto de espera removido; tarefa acordada pelo evento errado |
| B2 | Entrega | Multiconjunto e ordem de `(INTEVT, thread, função interrompida)` por VBlank iguais | IRQ extra; IRQ ausente; IRQ pós-`pref` removida |
| B3 | Recursos | CL idêntico (sequência e VBlank) | leitura omitida; leitura reordenada |
| B3b | Leituras de estado e relógio | CK idêntico byte a byte à referência (quantidade e hash por VBlank) | leitura de SPG/MDST/RTC/IST alterada ou omitida (mutação) |
| B4 | Provedores de arquivos | TN de `--arquivos legado` = TN de `nativo`; contador de VBlanks fundidos (VBlank-In com bit já aceso) = 0 nos dois | fusão provocada por mutação → contador > 0 e TN diferente |
| B5 | Esperas com timeout | Caminho tomado (fim × estouro) igual ao legado em todas as esperas | render que nunca termina → estouro em 166 ms lógicos, sem travar |
| B6 | Jogo | `test_gameplay` e SL iguais à referência | snapshot alterado (mutação de uma variável de posição) |
| B7 | Liveness | 0 guardas; deadlock com mensagem | laço sem espera → guarda com PC; fila vazia → erro |
| B8 | Determinismo | Duas execuções: TN, CL, SL, CK e quadro iguais | ordem dependente do host |
| B9 | Desempenho | Trabalho médio por campo ≤ N2a × 1,05 (3 execuções alternadas, mesma máquina) | regressão injetada (espera ativa no host) → falha |
| B10 | Mods e distribuição | Todas as suítes; mod de função de espera sem a primitiva dá erro claro | idem |
| B11 | TMU (D7) | Valores gravados pelos medidores do CRI comparados com a referência; ordem lógica (TN) inalterada | consumidor artificial que decide pelo TMU → TN diferente |

## 5. Preservação

- **Eventos e temporizadores:** prioridade da fila do N2a; temporizadores nativos para os
  timeouts. A ordem dos tratadores continua a do código recompilado.
- **Gameplay:** o escalonador do jogo e todas as funções do jogo ficam como estão. Só os
  back-edges declarados mudam de `BS_POLL` para `BS_WAIT`.
- **Controles:** o Maple continua amostrado pelo jogo a cada VBlank. O fim de DMA entra na fila
  (D9). Os roteiros de entrada são por VBlank, independentes de ciclos.
- **Mods:** a primitiva de espera fica na API; mods de recurso não são afetados.
- **Rollback:** `--tempo legado|nativo` (N2a) e `--tempo n2b` enquanto o N2b não virar padrão.
  As distribuições e os backups atuais ficam.

## 6. Ambiente de build (sem instalar nada, independente do idioma)

- Problema: o `/showIncludes` é traduzido e muda com a página de código do terminal. Builds de
  terminais diferentes perdem as dependências (N1/N2a).
- **Solução proposta:**
  - opção `BS_MSVC_DEPS_JSON`, já no CMake e **desligada**;
  - o compilador grava as dependências em JSON (`/sourceDependencies`);
  - `tools/build/msvc_srcdeps.cmake` converte o JSON para o depfile do ninja (`deps = gcc`);
  - nenhum texto traduzido no caminho.
- **Verificado:**
  - projeto mínimo com a página 850 num console separado: o modo atual perde a dependência
    (`#deps 0`), o JSON mantém (`#deps 1`);
  - o caso entrou no `n2_build_incremental`;
  - projeto inteiro: seção 11.
- Proposta (D10): ligar por padrão nos presets e no kit do Builder (incluir `tools/build`), depois
  de medir o custo (um processo de CMake por unidade de compilação).

## 7. Riscos

| Risco | Mitigação |
|---|---|
| **A thread 3 não tem espera** (servidor do CRI em laço até a preempção) | N2b-0: condição de ociosidade; S4 no modo sombra; sem ela, não há N2b-2 |
| Preempção do SDK depende do instante da IRQ | Entrega só em pontos de espera e de desmascaramento; B1/B2 |
| Carimbos do CRI alimentarem decisões (sincronia de vídeo) | Trace de leitura no N2b-0; roteiro com vídeo completo; B11 |
| Timeout que hoje nunca estoura passar a estourar (ou o contrário) | Temporizador lógico de 166 ms; B5 com estouro forçado |
| Fim de render instantâneo mudar o ritmo do passo de 2 VBlanks | D9 medida no N2b-1 |
| Mods que reescrevem funções de espera | Primitiva na API; erro claro |
| Cobertura (roteiro só até a 1ª área) | Roteiros novos no N2b-0; matriz do plano-mestre |
| Dependências de build | Opção `BS_MSVC_DEPS_JSON` (D10) |

## 8. Decisões que dependem do mantenedor
1. **D7:** TMU = relógio lógico, parado dentro do passo, com timeouts como pontos de espera.
   Por ora é **hipótese condicionada ao N2b-0** (seção 1.4), não decisão.
2. **D8:** guarda de liveness = erro com PC também no produto durante o N2b (recomendado depois
   da revisão). O modo degradado, em que o tempo anda pela guarda, só por opção explícita e
   documentado como não equivalente.
3. **D9:** instante lógico de fim de render, Maple e GD-ROM: próxima espera (recomendado para
   Maple e GD-ROM; o render é medido antes).
4. **D10:** ligar `BS_MSVC_DEPS_JSON` por padrão (presets e kit do Builder).
4b. **D4 revista:** substituir `ITaskScheduler` (que tem `yield` e `run_ready`) por uma interface
   só de espera e avanço do tempo, ou mantê-la reservada sem implementação.
5. **Escopo:** o N2b decide só quando o tempo anda. A escolha de thread fica com o despachante
   do SDK, e o escalonador do jogo fica em código do jogo (recomendado, pela regra de não mexer
   no gameplay).
6. **Thread 3:** aceitar que o N2b-2 depende de achar a condição de ociosidade do CRI. Sem ela,
   o N2b para no modo sombra.

## 9. Testes diagnósticos feitos nesta preparação
- `BS_CALL_TRACE` com PR e VBlank de entrada (mudança só no formato do diagnóstico; cabeçalho
  corrigido).
- `BS_TMU_DIAG=quadro` (diagnóstico, desligado por padrão). Usa o instante de **entrega** do
  último VBlank.
- `n2_build_incremental` com o modo JSON:
  - página 850 em console separado;
  - caminho com espaço, `#` e `$$` e um segundo build de verdade. Esse caso falha sem o escape;
    conferido revertendo-o;
  - compilador com erro: falha, sem objeto.
- A opção `BS_MSVC_DEPS_JSON` no CMake, desligada.
- Comportamento padrão inalterado: trace de ordem em 3 000 VBlanks = referência do N2a
  (`176F4C28…`).

Nenhuma mudança de agendamento ou de gameplay.

## 10. (reservado)

## 11. Build completo com `BS_MSVC_DEPS_JSON`

Árvore separada `build/msvc-json` (Release, jogo, `BS_MSVC_DEPS_JSON=ON`), 2026-10-15:
- configurou e compilou sem erro (1,1 min);
- 381 objetos;
- **0 objetos do projeto sem dependências**;
- os 31 sem dependências são do SDL3 (terceiros) e são os mesmos no modo atual: ao que parece,
  arquivos que só incluem o cabeçalho pré-compilado;
- o executável roda (120 VBlanks, rc 0).

Falta medir antes de ligar por padrão (D10):
- custo de um build incremental;
- o Builder com o kit.

## 12. Revisão do Codex

`reviews/codex-20261015-192135.md` (somente leitura): 2 ALTA, 4 MÉDIA, 1 BAIXA. Todos
conferidos no disassembly ou no código e aceitos.

| Gravidade | Achado | Decisão |
|---|---|---|
| ALTA | `BS_WAIT` no laço da thread 3 mudaria o número de chamadas ao servidor do CRI por VBlank | **Aceito.** A thread 3 sai da lista de esperas; depende de condição de ociosidade (N2b-0) e do critério S4 (seções 1.1, 3.2, 3.4) |
| ALTA | Guarda que avança o tempo contradiz "sem tarefa apta"; `BS_WAIT` não pode escolher thread | **Aceito.** `BS_WAIT` nunca escolhe thread (a troca é do despachante, `rte` em `8C17DFB6`); guarda = erro também no produto; modo degradado só explícito e não equivalente (3.4, D8) |
| MÉDIA | Evidência do TMU não sustenta a D7 nem a classificação completa | **Aceito.** D7 vira hipótese condicional; limites registrados; trace de propagação e ensaio sem ciclos no N2b-0/1 |
| MÉDIA | B1–B11 sem oráculo ou negativo em vários pontos | **Aceito.** Artefatos TN/CL/SL/CK, igualdade com multiplicidade e um negativo por critério (seção 4) |
| MÉDIA | Depfile sem escape de `#` e `$` | **Aceito.** Escape conforme o parser do ninja; teste com `$$` no caminho e segundo build, que falha sem a correção |
| MÉDIA | Cabeçalho do call trace e VBlank lido no retorno | **Aceito.** Cabeçalho `… r0 pr vblank strings`; VBlank lido na entrada |
| BAIXA | `cmake_language(EXIT)` exige CMake 3.29 | **Aceito.** `message(FATAL_ERROR)`; teste negativo com compilador falhando |


**Acompanhamento** (`reviews/codex-20261015-192954.md`): 1 ALTA e 2 MÉDIA, todos de texto e
aceitos.

| Gravidade | Achado | Decisão |
|---|---|---|
| ALTA | As seções 2 e 3.1 ainda atribuíam a escolha de thread ao agendador nativo; `ITaskScheduler` permite troca sem `rte` | **Aceito.** O componente nativo só faz o tempo andar e entrega eventos; a seleção é do `rte` recompilado; `ITaskScheduler` não é usada para as threads do SDK (D4 revista) |
| MÉDIA | TMU: "início do VBlank" e "classificação completa" | **Aceito.** "Classificação preliminar"; experimento descrito como congelamento no instante de entrega; prova no início lógico fica para o N2b-0 |
| MÉDIA | CK sem oráculo nem negativo | **Aceito.** Novo critério B3b (CK byte a byte, com mutação) e CK no B8 |
