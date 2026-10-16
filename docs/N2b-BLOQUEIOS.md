# N2b-2: investigação dos bloqueios (relatório)

Data: 2026-10-16. Base: N2b-0/N2b-1 (`18a5cc2`). Planos: `docs/N2b-PLANO.md`,
`docs/N2b-RELATORIO.md` (seção 8, bloqueios).

Pedido do mantenedor: eliminar os bloqueios do N2b-2 **sem implementá-lo**. Sem N2c, sem esperas
fictícias, sem mudar o escalonador do gameplay, sem emulação no produto. **Sem commit.**

Tudo o que foi acrescentado ao runtime é diagnóstico, desligado por padrão, e não muda a
execução (provado pelo critério C1, seção 7). As exceções são:
- a primitiva de espera na API de mods (B10), que é API nova;
- a correção do artefato de áudio (seção 3);
- o aviso imediato de falta de progresso, com a thread e o PC (`TimeProvider::progress` recebe o
  PC), que só acrescenta uma linha de saída.

## 1. Resumo

| Bloqueio (relatório do N2b-1) | Situação | Evidência |
|---|---|---|
| 1. Ociosidade da thread 3 no gameplay | **Comprovada nesta rota, por medição e amostragem** | Seção 2: no gameplay, nenhuma passada com trabalho; numa amostra de 6 janelas, nenhuma passada ociosa sem evento muda a RAM (principal e de som), exceto os medidores do TMU, e com a D7 nem eles; bloquear até o próximo evento preserva todos os artefatos lógicos nas duas rotas |
| 2. Áudio dependente do tempo | **Era defeito de medição.** Oráculo definido, com negativos de comparador e de sensibilidade | Seção 3: 99,8% do "AUD" eram escritas do P4. As escritas e leituras reais na AICA são iguais em todos os experimentos. O estado do driver de som é uma entrada da lógica do jogo |
| 3. B10 e B11 | **Cobertos**, com a classificação dos medidores corrigida | Seção 4 |
| 4. D8 em mais rotas | **Parcial** | Seção 5: rota de 16 000 VBlanks, mesma janela máxima; não há roteiro reproduzível com troca de sala ou save/load |
| 5. B4 entre provedores de arquivos | **Diagnóstico e proposta** | Seção 6: a sequência de eventos de lógica do jogo é idêntica; só muda o VBlank nas cargas |
| 6. "Liberada por" × transição da condição | Sem mudança | Não pedido nesta etapa |

**Revisão do Codex** (`reviews/codex-20261016-105307.md`): 1 ALTA, 4 MÉDIAS, 2 BAIXAS, todas
aceitas e corrigidas (seção 10).

**Avaliação (seção 9):**
- os bloqueios que dependiam de evidência foram resolvidos dentro do que esta rota permite medir;
- o resto são decisões do mantenedor e duas limitações conhecidas: os comandos de som no gameplay
  ficam invisíveis sem o ARM7, e a D8 só foi medida em duas rotas.

## 2. Thread 3 (servidor do CRI) no gameplay

### 2.1 Gerenciador de streams (disassembly)

O controle fica em `0x8C2DA958`:

| Campo | Significado |
|---|---|
| `+0` | quantidade (16) |
| `+4` | base das entradas |
| `+8` | leitura em curso (acompanhada por `8C174C7C`) |
| `+12` / `+20` | tamanho e progresso do pedido (`8C174E16`) |
| `+36` | índice do rodízio (`8C174A36`) |

Cada entrada tem 68 bytes:

| Campo | Significado |
|---|---|
| `+0` | em uso |
| `+4` | estado (2 = pode ler) |
| `+12` | limiar |
| `+48` | **buffer cheio**, não "pedido pendente" (correção do N2b-1) |
| `+60` | handle |

O `+48` funciona assim:
- `8C174888` o acende quando `8C19136E` não acha espaço;
- `8C174998` o apaga quando o predicado `8C1749E0` vê o nível do buffer (`8C19135A`, função
  virtual do handle) no limiar ou abaixo.

Uma chamada do gerenciador:
1. leitura em curso: acompanha;
2. senão, pedido em andamento: continua (`8C174BAA`);
3. senão, apaga os "cheios" esvaziados;
4. escolhe uma entrada (em uso, sem "cheio", estado 2);
5. emite a leitura, ou marca "cheio".

O índice `+36` anda em toda chamada, mas só decide quando há mais de uma candidata. Isso nunca
aconteceu ("empates no rodízio 0").

### 2.2 Medição (`BS_CRI_DIAG=fluxo`)

A medição classifica cada passada (`8C1586B8`) como "com trabalho" se ocorreu pelo menos um
destes eventos:
- MW/ADX ativo;
- leitura em curso;
- emissão;
- buffer esvaziado;
- candidata escolhida;
- estado do gerenciador mudado em qualquer ponto da passada.

**Não invasiva:** os arquivos brutos (trace de ordem com ciclos, leituras de arquivo, relógios e
AICA, estado, quadro) são idênticos byte a byte com e sem todos os diagnósticos (C1, área 1). Na
exploração, as projeções normalizadas também são iguais.

| Rota | Passadas | Com trabalho | Ociosas | Passadas com stream aberto (ociosas) |
|---|---|---|---|---|
| Área 1, 8 000 VBlanks | 2 666 236 | 90 650 | 2 575 585 | 1 948 852 (1 858 204) |
| Exploração, 16 000 VBlanks | 5 335 334 | 90 650 | 5 244 683 | 4 617 950 (4 527 302) |

- **O trabalho é igual nas duas rotas.** Os 8 000 VBlanks a mais de gameplay da exploração
  somam 2,67 M passadas e **nenhuma** com trabalho.
- 90 617 das passadas com trabalho são do vídeo (MW/ADX). Fora do vídeo, há cerca de 34 em
  8 000 VBlanks: abertura, fechamento e enchimento inicial de streams.
- Depois da última leitura de arquivo (V4653), nenhuma passada trabalha (C2).
- O stream ambiente do gameplay é o `E012BENV.WAV`:
  - lê 48 blocos (V4641–V4653), enche o buffer (`+48` = 1) e para;
  - o nível só muda 54 vezes na rota inteira (0 a 393 216 bytes);
  - **nada o consome no gameplay**: sem o ARM7, ninguém toca o stream, e o consumo por relógio
    de software (seção 2.4) só aconteceu no enchimento.

### 2.3 Observador independente: toda a RAM (`BS_CRI_SNAP_VB`)

A captura compara **toda a RAM principal (16 MB) e a RAM de som (2 MB)** antes e depois de cada
passada (revisão do Codex: duas janelas da RAM não bastavam). Ficam de fora só:
- **a pilha morta:** de SP − 16 KB até o SP da entrada da passada. Isso fica abaixo do topo da
  pilha depois do retorno: são quadros da própria passada e de tratadores que já voltaram;
- **o índice do rodízio** (`+36`), que anda em toda chamada e só decide com empate.

Os medidores do TMU contam à parte.

| Janela (3 VBlanks) | Passadas | Ociosas | Ociosas com RAM mudada | Delas, sem evento durante a passada | Ociosas sem evento que só mudaram medidores |
|---|---|---|---|---|---|
| Menu (V1000) | 686 | 686 | 2 | **0** | 683 |
| Fim do vídeo (V2255) | 1 848 | 1 848 | 3 | **0** | 1 842 |
| Enchimento do stream ambiente (V4649) | 1 334 | 1 329 | 3 | **0** | 1 324 |
| Gameplay da área 1 (V6000) | 668 | 668 | 2 | **0** | 665 |
| Gameplay da exploração (V12000) | 667 | 667 | 2 | **0** | 664 |
| Gameplay da área 1, **TMU parado por quadro** (D7) | 669 | 669 | 2 | **0** | **0** |

Leitura:
- As poucas mudanças vêm de passadas em que houve evento no meio. Exemplos:
  - o callback de fim de render `8C158A82` incrementa `0x8C1DD240` e troca o anel de descritores
    em `0x8C2CFB6C`;
  - a preempção deixa a thread principal rodar e mudar o estado dela.
- Sem evento, uma passada ociosa só grava os **medidores do TMU**: as medições de duração dos
  sub-servidores. O B11 mostra que nenhuma decisão depende deles.
- **Com o TMU parado por quadro (D7), uma passada ociosa sem evento não muda nenhuma palavra das
  duas RAMs.**
- **Limites:**
  - é uma amostra de 6 janelas;
  - o MMIO e o estado interno do runtime (fila de eventos, HLE do GD) não estão na captura.
    Eles aparecem nos artefatos (CK, IST, CL), que ficam iguais (C3).

### 2.4 Condição de ociosidade e eventos que acordam

**Condição** (avaliada depois da passada, pelos detectores acima): nada disso aconteceu
- nenhuma entrada MW nem handle ADX ativo;
- nenhuma leitura em curso nem emitida;
- nenhum buffer esvaziado, nenhuma candidata;
- nenhuma mudança no gerenciador.

**Passadas "elegíveis"** são as que vêm depois de uma ociosa sem que nada tenha acontecido. A
época de eventos inclui:
- interrupção aceita;
- troca de thread;
- VBlank-In;
- evento do Holly entregue, mesmo mascarado;
- bloco de GD-ROM.

**Violação:** passada elegível que trabalhou sem nenhum evento, nem antes nem durante.

| Medida (8 000 VBlanks) | Violações |
|---|---|
| Época verificada só no início da passada | 7 (V2255, V2270, V2274, V4474, V4639, V4649, V4651: abrir, fechar e encher streams) |
| Também durante a passada | **1** (V4649: consumo de `0x6000` bytes do buffer ambiente, o nível em `0x8C2DACE0` cai de `0x602E` para `0x2E`) |
| E com o TMU parado por quadro (`BS_TMU_DIAG=quadro`, modelo da D7) | **0** |

- A única transição sem evento some quando o TMU fica parado dentro do quadro. Logo, ela depende
  do TMU (o relógio de software do stream). Isso é inferido pelo experimento, não lido no
  binário.
- Com o TMU como relógio lógico (D7), **todo trabalho da thread 3 nesta rota é disparado por
  evento**.

**Como as threads se alternam hoje** (disassembly; os ids estão em `[0x8C2D1D20]` para a thread
principal e `[0x8C2D1D1C]` para a thread 3):
1. A cada render, `8C158864` (registrada por ponteiro em `0x8C158C98`) roda na thread principal:
   - copia os carimbos;
   - se a bandeira lógica `[0x8C1DD23C]` estiver acesa, copia mais;
   - **cede a vez à thread 3** (`8C17DDCC`).
2. A thread 3 gira passadas.
3. O callback de VBlank (`8C158A26`):
   - incrementa o contador lógico `0x8C1DD1C4` e acende `0x8C1DD23C`;
   - quando o contador atinge `[0x8C1DD1B4]`, pede ao despachante (`8C17DFE8`) a troca de volta
     para a thread principal.

A alternância já é movida por eventos no próprio jogo. Só o giro da thread 3 no meio é que vira
espera.

**Eventos que acordam a thread 3** (disassembly):
- **Cessão a cada render:** `8C158864`, acima.
- **VBlank:** callback `8C158A26`, acima.
- **Fim de render:** callback `8C158A82` (`[0x8C2D9E3C]` = 0, `0x8C1DD240`++).
- **Bloco de GD-ROM vencido:** faz parte da época de eventos medida.
- **Ações da thread principal:**
  - abrir e fechar streams;
  - iniciar e parar o MW, que pega a exclusão `8C1588AA` (seção 2.6).

  Elas só acontecem com a thread 0 rodando, ou seja, depois de uma troca.
- **Consumo do stream:** hoje, só pelo TMU (V4649); com a D7, no avanço do tempo lógico; com o
  áudio nativo, num evento de áudio (`AudioTick`).
- **Leitura de arquivo:** no provedor nativo, conclui na chamada seguinte ao servidor do gdFs,
  que já conta como trabalho.

### 2.5 Experimento e controle

Os modos são:
- `pular5` não executa as passadas elegíveis, o que equivale a bloquear a thread 3 até o próximo
  evento;
- `repetir5` executa duas vezes as mesmas passadas (só o tempo muda).

Com a época de eventos completa (inclui eventos do Holly entregues com a interrupção mascarada
e blocos de GD-ROM), build final:

| Rota | Comparação com a referência | `pular5` (bloqueio) | `repetir5` (controle) |
|---|---|---|---|
| Área 1 (8 000) | TN (sem normalização), IRQF, CL (com VBlank), SL, AUD, quadro | **iguais** | iguais |
| | CK, SPG (tempo) | diferem | diferem |
| Exploração (16 000) | TN (sem normalização), IRQF, CL, SL, AUD, quadro | **iguais** | iguais |
| | CK, SPG (tempo) | diferem | diferem |

Nas primeiras rodadas, a época não incluía os eventos do Holly mascarados. O `pular5` pulava
passadas que vinham depois deles, e a TN diferia em 2 eventos (uma cessão de exclusão mútua em
V2096). Com o conjunto completo de eventos de despertar, essa diferença sumiu nas duas rotas.

O negativo (`pular`) usa uma condição de ociosidade errada, que ignora o gerenciador de streams.
Ele é **detectado** nas leituras (CL).

### 2.6 A única diferença de ordem: cessão de exclusão mútua

A thread 3 acende `[0x8C1DD1BC]` = 1 durante cada passada (`8C158858`..`8C158862`). A thread
principal, em `8C1588AA` (chamada pelo método de parada de handle do MW, `8C158250`, via tabela
em `0x8C1DD184`):
1. acende `[0x8C1DD1C0]`;
2. **enquanto a thread 3 estiver no meio de uma passada**, cede a vez a ela (`8C17DDCC`);
3. a thread 3 termina a passada, vê `[0x8C1DD1C0]` = 1 e devolve a vez.

O resultado é um par `T 0→3, T 3→0`:
- ocorre 4 vezes em 8 000 VBlanks (V2096, V2251, V4480, V4627);
- só existe quando a preempção por ciclos interrompe a passada no meio;
- se a passada sempre termina antes de a thread 3 parar, o par some.

**Causa marcada uma a uma** (segunda revisão do Codex: o intervalo da chamada não era causal):
- `BS_CRI_CESSAO_LOG` envolve a cessão `8C17DDCC`;
- só para os dois chamadores da exclusão, registra o número de sequência do próximo evento do
  trace:
  - PR `8C1588C2`, a thread principal no laço de `8C1588AA`;
  - PR `8C158854`, a thread 3 devolvendo porque `[0x8C1DD1C0]` = 1;
- `8C17DDCC` mascara as interrupções e salta ao despachante, então esse evento é a troca que ela
  pediu;
- a cessão de cada render (`8C158864`) e as trocas pedidas pelo callback de VBlank não entram.

A TNC remove só os eventos marcados, e o teste falha se alguma marca não cair numa troca:
- no `test_cri`, 8 marcas na referência e 8 no bloqueio, todas em trocas;
- e a TN sem normalização já é igual.

Fica proposta como exceção fechada, a decidir pelo mantenedor (`config/n2b_excecoes.toml`). Nesta
rota ela não foi necessária, mas o mecanismo pode produzi-la quando o bloqueio real mudar os
pontos de preempção.

### 2.7 O critério S4 precisa mudar

O S4 do plano pede a quantidade e a ordem das chamadas ao servidor **por VBlank**. Pelo controle
`repetir5`, que não omite trabalho nenhum, isso é um artefato de tempo:
- a distribuição das passadas com trabalho de vídeo muda em 94 VBlanks;
- e mesmo assim TN, CL, SL, AUD e o quadro ficam iguais.

Proposta: o S4 vira **artefatos lógicos (TNC, IRQF, CL, SL, AUD, quadro) iguais** e passa a
incluir o negativo de ociosidade errada.

A TN sozinha não detecta trabalho perdido da thread 3: o `menos1` do N2b-1 só muda artefatos de
tempo. Quem detecta são CL e AUD.

## 3. Oráculo de áudio

**Defeito no artefato AUD do N2b-0 (corrigido):**
- O `hwWrite` tenta o `aica_reg_write` antes dos handlers do P4, e ele registrava no AUD toda
  escrita de 32 bits, mesmo as que não eram dele.
- Em 8 000 VBlanks:
  - 424 346 263 dos ~425 milhões de "escritas de áudio" eram escritas do P4 (store queues com
    dados de vídeo e vértices, TMU, INTC);
  - as escritas reais na AICA são **1 023 386**.
- Agora só entram no AUD:
  - a RAM de som;
  - os registradores `0x00700000`–`0x0071FFFF`.
- A execução não muda: essas escritas já caíam no mesmo `return false`.
- **A conclusão do N2b-0/N2b-1 de que "as escritas na AICA durante o vídeo dependem do tempo de
  execução" estava errada.** Quem mudava eram as store queues.

**Escritas reais** (área 1):

| Origem | Escritas |
|---|---|
| Boot (envio do driver de som) | 973 834 |
| Vídeo (V2000–V2500) | 24 800 |
| Carga da área (V4000–V4500) | 24 752 |
| Gameplay (área 1 até V8000; exploração de V5000 a V12000) | **0** |

Por que o gameplay não escreve nada:
- A biblioteca de som lê, a cada quadro, o bloco de estado do driver (`0x00813A00`,
  `0x00813A18`, `0x00813400`), pela rotina de transferência G2 (`8C08EC4A`).
- Sem o ARM7, esse bloco guarda o preenchimento "SEGA" do boot.
- **Comprovado pelo negativo de sensibilidade:** com o preenchimento de `0x00813A00` não
  aplicado (o bloco fica em 0), a partir do vídeo (V2117):
  - a biblioteca passa a mandar 16 escritas por quadro;
  - **o vídeo muda**: 8 447 leituras de arquivo em vez de 7 750, e estado e quadro diferentes.
- O estado do driver de som é, portanto, **uma entrada da lógica do jogo**, inclusive da sincronia
  do vídeo. Isso reforça a hipótese "vídeo termina cedo: relógio do áudio", ainda em aberto no
  STATUS, mas não a confirma.
- O áudio nativo terá de reproduzir essa semântica. A referência atual (bloco "SEGA") é um
  artefato de o ARM7 não rodar.

**Oráculo** (`AUD`, colunas 11 a 14 do `BS_CLOCK_LOG`), por VBlank:
- quantidade e hash das escritas da CPU na RAM de som e nos registradores da AICA;
- **e das leituras** da RAM de som, porque o estado lido decide o que a biblioteca faz.

Exige igualdade exata nas comparações abaixo (A1), e os negativos têm de ser detectados:

| Comparação | Resultado |
|---|---|
| Medição | igual |
| Bloqueio (`pular5`) | igual |
| Controle (`repetir5`) | igual |
| Tempo legado × modo sombra | igual |
| TMU parado por quadro | igual |
| Negativo do comparador: `BS_AICA_MUTACAO=990000` perde uma escrita no envio de bancos (e ela sai do log) | detectado só no AUD |
| Negativo de sensibilidade: `BS_AICA_MUTACAO=registrada:00813A00` registra a escrita mas não a aplica | detectado pelas leituras seguintes (e por TN, CL, SL, quadro) |

O primeiro negativo só prova que o comparador funciona, porque a mutação tira a escrita do log
(revisão do Codex). O segundo prova a sensibilidade: o log fica igual no ponto da mutação, e só o
comportamento posterior da CPU a revela.

**Limitação:** o oráculo cobre o envio do driver e dos bancos e a consulta de estado. Os
comandos de som do gameplay não chegam à AICA sem o driver respondendo.

Quando houver áudio nativo, o oráculo passa a ser a sequência de comandos da biblioteca de som
(funções da API por slot). Esse áudio nativo tem de:
- reimplementar o Manatee (decisão de 2026-10-15);
- responder no bloco de estado.

## 4. B10 e B11

**B10, mod de função de espera:**
- **API nova** (`runtime/mods/mod_api.h`):
  - `bs::mods::wait(c, pc, resume, ciclos)` = `BS_CYCLES` + `BS_WAIT`;
  - `wait_end(c, pc)` = `BS_WAIT_END`;
  - `poll(c, resume, ciclos)` = `BS_CYCLES` + `BS_POLL`.

  São as mesmas macros do código gerado; por isso a catraca do N0 não acusa consumidor novo de
  ciclos.
- Unitário: `n2b_mod_wait_primitive`.
- `test_projeto.ps1` (caso 4b) compila no projeto editável um mod que reescreve a espera do
  VBlank (`8C064A20`):
  - **com a primitiva**, roda os 2 300 VBlanks, a espera `8C064A2C` aparece com o evento
    esperado e não há falta de progresso;
  - **sem a primitiva** (só poll), aparece a mensagem "falta de progresso: 64000046 pontos de poll
    … na janela: thread 0 57504998 (maior lote em 8C064A26), …, thread 3 6494060 (maior lote em …).
    Um laço de espera reescrito (mod) tem de usar bs::mods::wait() em cada volta".
- O aviso sai na primeira falta e lista, para cada thread na janela, o trabalho e o PC do
  checkpoint que recebeu o maior lote. Os pontos de controle entre dois checkpoints são creditados
  ao PC do checkpoint (D8: erro com PC no N2b-2). A regressão pegou dois erros nas versões anteriores:
  1. nomear a thread que cruzou o limite: era a thread 3, que no N2b-1 nunca passa por um ponto
     de espera;
  2. usar o último PC: era o ponto onde a thread 0 foi suspensa (a cessão ao CRI a cada render),
     não o laço do mod.

**B11, TMU:**
- **Medidores do CRI** (`0x8C1DD1C8`–`0x8C1DD238`, `BS_CRI_MEDIDORES`). Só o módulo do CRI
  (`8C1584xx`–`8C158Bxx`) referencia a área por endereço literal (o Codex achou leitores; a
  classificação foi corrigida):
  - `0x8C1DD1C4` **não é medidor**: é o contador lógico de VBlanks do callback `8C158A26`. Ele é
    lido e comparado com `[0x8C1DD1B4]` para pedir a troca de thread, por isso saiu da faixa;
  - `0x8C1DD23C` é uma bandeira lógica, acesa pelo callback de VBlank e lida por `8C158864`
    (também saiu da faixa: os medidores são `0x8C1DD1C8`–`0x8C1DD238`);
  - os carimbos do TMU `0x8C1DD228` e `0x8C1DD238` **são lidos** por `8C158864`, mas só para
    serem copiados em `0x8C1DD224` e `0x8C1DD230`. Nenhum desvio depende deles;
  - `8C158AB4` só inicializa (limpa o cache e o CCR).
- Leitores por base+deslocamento não foram excluídos pela análise estática. A evidência de que os
  carimbos não decidem nada é dinâmica: com o TMU parado, a lógica fica igual.

| Caso (5 000 VBlanks) | Resultado |
|---|---|
| Medidores, tempo legado × modo sombra | **iguais**, e todos os outros artefatos também |
| TMU parado por quadro | TN, IRQF, CL, SL, AUD e quadro **iguais**; medidores diferentes (medem tempo, e nenhuma decisão depende deles nesta rota) |
| Consumidor artificial (`BS_CRI_DIAG=consumidor`, decide pelo hash dos medidores), legado × sombra | iguais (controle) |
| Consumidor artificial com o TMU parado | **detectado** (nas leituras, com um bloco de `NEWGAME.SFD` mudando de VBlank, e conforme a execução também na TN). O comparador tem de ser o conjunto TN+CL+SL+quadro |

## 5. D8 (limite de falta de progresso)

As maiores janelas sem ponto de espera estão todas em cargas:

| VBlank | Janela | Contexto | Mais trabalho na janela |
|---|---|---|---|
| V4630 | 17 232 307 | entrada na área | thread 3 |
| V2254 | 16 960 288 | início do vídeo | thread 3 |
| V34 | 16 619 172 | boot | thread 0 |

- A rota de exploração de 16 000 VBlanks não passa disso: o gameplay longo não aumenta a janela.
- Com o provedor legado de arquivos: 17 232 950.
- As mutações que travam o jogo dão de 744 M a 2,85 G pontos de controle, o que o limite de 64 M
  detecta.
- **Limitação:** não há roteiro reproduzível com troca de sala nem com save/load.
  - Uma tentativa às cegas (caminhar até a porta do C-GATE) não chegou lá: o controle gira o
    personagem.
  - Proposta: um gravador de entrada (`BS_INPUT_RECORD`, mesmo formato do `BS_INPUT_SCRIPT`)
    para gravar rotas jogando.
- No N2b-2 a composição da janela muda, porque a thread 3 deixa de girar durante as cargas. O
  limite deve ser recalibrado no próprio N2b-2, com a mesma medição.

## 6. B4: ordem lógica entre os provedores de arquivos

TN de `--arquivos legado` × `nativo` (tempo legado, 8 000 VBlanks): mesma quantidade de eventos
(116 893) e só 9 blocos diferentes, todos interrupções Holly `0x320` que mudam de VBlank.

| Visão | Resultado |
|---|---|
| Eventos de lógica do jogo (trocas, threads novas, `longjmp`: 71 790) | **sequência idêntica**. O legado só fica um ciclo do escalonador atrás: um bloco de 18 eventos, que no boot (V34) leva um passo a mais |
| VBlank dos mesmos eventos | muda só em 4 pontos de carga (V221, V2091, V2253, V4629) |

**Causas:**
- **Latência simulada do drive no provedor legado** (provável: o GD-ROM legado entrega os blocos
  pelo tempo, `reiosTicks`): as cargas terminam mais tarde.
- **Preempção por ciclos.** Na V221, a ordem entre um `longjmp` do escalonador do jogo e a troca
  3→0 depende de onde a interrupção acerta.

**Proposta (sem atrasos artificiais):**
1. Até o N2b-2, o B4 entre provedores continua sendo o contrato P1 (CL), e entra o critério
   informativo **B4'**: sequência de eventos de lógica idêntica, com deslocamentos de VBlank só
   em janelas de carga.
2. No N2b-2:
   - Com a D9(a), o GD-ROM conclui o bloco no próximo ponto de espera nos **dois** provedores. Em
     vez de atrasar o nativo, a latência simulada do legado sai.
   - Sem a preempção por ciclos, a ordem `longjmp`×troca passa a depender só dos pontos de
     espera.
   - O B4 com TN completa vira o portão do N2b-2.

## 7. Testes

Regressão definitiva (2026-10-16, código congelado antes do início):

| Suíte | Resultado |
|---|---|
| Builds Release, Debug e mods | ok |
| Unitários | 211/211 |
| `ctest` | 12/12, sem e com a imagem (`n0_fechamento` incluso) |
| `make_builder`, `make_preview` | ok (Preview com o código desta etapa; backups intactos, incluindo `.n2b01-18a5cc2`) |
| `test_preview` | 9/9 |
| `test_gameplay` | 4/4 |
| `test_builder` | 18/18 |
| `test_projeto` (mods, B10) | 13/13 |
| `test_arquivos` | 6/6 |
| `test_ordem` | 8/8 |
| `test_tempo` | 35/35 |
| `test_n2b` | 44/44 (B9: 16,99 × 17,18 ms por campo) |
| `test_cri` | 33/33 |


Critérios do `test_cri.ps1`:

| Critério | O que exige |
|---|---|
| C1 | os arquivos brutos com todos os diagnósticos = os da referência, byte a byte |
| C2 | depois da última leitura, nenhum trabalho; violações existem e são transições de stream; zero com o TMU parado, com eventos durante as passadas observados; observador independente (RAM principal e de som) sem mudança nas passadas ociosas sem evento |
| C3 | bloqueio = referência (TNC com causa instrumentada, IRQF, CL, SL, AUD, quadro) |
| C4 | controle de tempo = referência |
| C5 | negativo da ociosidade errada detectado |
| A1 | oráculo de áudio igual em todos os modos; negativo do comparador e negativo de sensibilidade |
| B11a, B11b, B11c | medidores, TMU parado e consumidor artificial |

## 8. Limitações

- **A completude da condição de ociosidade é amostral:**
  - medida em 6 janelas de 3 VBlanks com toda a RAM principal e de som;
  - fora delas, ela é inferida pelos detectores do gerenciador de streams e pelo experimento C3;
  - o conjunto de escrita dos alvos indiretos da passada não foi derivado estaticamente.
- **O conjunto de eventos que acordam** foi validado nesta rota (a primeira versão, incompleta,
  mudava a TN). Outra rota pode exercitar uma fonte de evento ainda não observada.
- **`pular5` aproxima o bloqueio, mas não o reproduz.** No tempo por ciclos, a thread 3 continua
  girando, só que sem trabalho. O N2b-2 muda quando o tempo anda, e precisa da própria
  comparação.
- **Comandos de som no gameplay** não chegam à AICA sem o ARM7 (seção 3). O estado do driver é
  uma entrada da lógica, então o áudio nativo vai mudar a referência.
- **D8 em duas rotas**, sem troca de sala nem save/load.
- **Uma rota de gameplay longo** (exploração da primeira área), com áudio ambiente mas sem
  combate nem troca de sala.
- **Medidores do TMU:** a ausência de leitores por base+deslocamento é inferida da evidência
  dinâmica (TMU parado → lógica igual), não provada estaticamente.

## 9. Avaliação: dá para começar o N2b-2?

**Um N2b-2 experimental e de escopo controlado é viável. A equivalência ainda não está fechada.**
Para começar, o mantenedor decide:
1. **S4 redefinido** (seção 2.7) e a **exceção** da cessão `8C1588AA`, com causa marcada evento a
   evento (seção 2.6). Nesta rota ela não foi necessária: a TN do bloqueio é igual sem
   normalização.
2. **D7 junto com o bloqueio da thread 3.**
   - Com o TMU andando dentro do quadro, sobra uma transição sem evento (V4649), que o
     bloqueio só adia até o próximo evento, sem efeito lógico (C3).
   - Com a D7, não sobra nenhuma, e a passada ociosa sem evento não muda a RAM.
3. **Condição de bloqueio da thread 3 = passada ociosa pelos detectores do `fluxo`**:
   - avaliada pelo host no back-edge do laço `8C158840`;
   - acorda no próximo evento: interrupção, troca de thread, VBlank, evento do Holly (mesmo com a
     interrupção mascarada) ou bloco de GD-ROM. Com um conjunto menor, a ordem muda (seção 2.5).

   Não é uma espera fictícia: o tempo só anda até o próximo evento da fila, e a escolha de
   thread continua com o despachante do SDK, que já alterna as threads por evento (seção 2.4).
4. **Oráculo de áudio em dois níveis:** AICA agora; comandos da biblioteca de som quando houver
   áudio nativo.
5. **D8 recalibrado durante o N2b-2**, e roteiros com troca de sala e save/load antes do N2c.

O que **não** está provado e seria o primeiro portão do N2b-2:
- a igualdade de TNC/CL/SL/AUD/quadro com o bloqueio **real**;
- a completude da condição de ociosidade fora das janelas amostradas;
- o B4 com TN completa entre os provedores de arquivos.

## 10. Revisão do Codex

Relatório: `reviews/codex-20261016-105307.md`.

O Codex confirmou como corretos:
- o significado do `+48` e o fluxo do gerenciador;
- a mecânica de `8C1588AA`;
- a correção do AUD;
- a primitiva de mods;
- o mod do caso 4b;
- os contadores novos e a assinatura de `progress`.

| Achado | Decisão | Ação |
|---|---|---|
| [ALTA] A TNC removia qualquer par adjacente 0→3→0, sem provar a causa | **Aceito** | `BS_CRI_CESSAO_LOG` registra o intervalo de eventos de cada chamada a `8C1588AA` com troca, e a TNC remove só as trocas de dentro (8 e 8 no `test_cri`) |
| [MÉDIA] "Medidores e carimbos são só escritos" era falso | **Aceito** | `0x8C1DD1C4` é contador lógico (saiu da faixa); `0x8C1DD23C`, bandeira lógica; os carimbos 228/238 são lidos e copiados por `8C158864`, sem decisão. B11 reescrito |
| [MÉDIA] C1 comparava projeções, não arquivos | **Aceito** | C1 compara os arquivos brutos (ev, fl, cl, sl, quadro): idênticos byte a byte |
| [MÉDIA] C2 circular, com tolerância de 10 VBlanks e passando com 0 = 0 | **Aceito** | Tolerância removida (o último trabalho cai no VBlank da última leitura). Exige violações > 0 e eventos durante as passadas > 0 com o TMU parado. Observador independente: toda a RAM numa janela de gameplay |
| [MÉDIA] A prova pela RAM cobria duas janelas e excluía demais da pilha | **Aceito** | Toda a RAM principal e a de som; exclusão só da pilha morta abaixo do SP da entrada; texto reescrito como evidência amostral (seções 2.3 e 8) |
| [BAIXA] O negativo do AUD tirava a escrita do próprio log | **Aceito** | Mantido como teste do comparador. Novo negativo de sensibilidade (`registrada:00813A00`), detectado pelas leituras seguintes; revelou que o estado do driver de som é entrada da lógica |
| [BAIXA] A lista de eventos de despertar omitia o GD-ROM | **Aceito** | Incluído (seções 2.4 e 9) |

**Segunda revisão** (`reviews/codex-20261016-141648.md`), só das correções:

| Achado | Decisão | Ação |
|---|---|---|
| [ALTA] A TNC por intervalo da chamada continuava temporal: uma troca pedida pelo callback de VBlank cairia no intervalo | **Aceito** | Marcação causal na cessão `8C17DDCC`, só para os PRs `8C1588C2` e `8C158854`; o teste falha se uma marca não cair numa troca |
| [MÉDIA] `0x8C1DD23C` (bandeira lógica) ainda estava na faixa dos medidores | **Aceito** | Faixa `0x8C1DD1C8`–`0x8C1DD238` |
| [MÉDIA] O negativo `registrada:` não exigia a consequência de comportamento | **Aceito** | Exige leituras diferentes e ao menos um de TN, CL, SL, quadro diferente (mudam os quatro) |
| [BAIXA] "PC onde mais gastou" é atribuição por lote | **Aceito** | Mensagem e texto: "maior lote em" |
| [BAIXA] A captura de RAM com o TMU parado não estava na regressão | **Aceito** | `fluxo_tq` com captura; exige zero mudanças sem evento e zero só nos medidores |

**Terceira verificação** (`reviews/codex-20261016-144322.md`):
- confirmou as cinco correções e a premissa causal no disassembly (`8C17DDCC` mascara,
  `8C17DFB6` faz o `rte`, PRs corretos);
- um achado BAIXO, aceito: exigir marcas também no bloqueio, em quantidade igual à da
  referência.
