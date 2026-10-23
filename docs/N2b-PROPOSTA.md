# Proposta: sequência técnica para destravar o N2b-2

Data: 2026-10-16. Base: `246e6b3` (investigação dos bloqueios, `docs/N2b-BLOQUEIOS.md`).

Autorização: proposta aprovada (2026-10-16); investigação P0/P1 autorizada, implementação não.
**Não implementar N2b-2 nem N2c. Não substituir áudio ou vídeo. Sem commit.** Resultados da
investigação nas seções 8 a 12 (inventário detalhado em `docs/N2b-ESPERAS.md`).

> **2026-10-17:** o mantenedor encerrou a investigação e autorizou o N2b-2 **só em modo
> experimental** (`--tempo experimental`). O P3 foi feito assim, com o padrão inalterado. Relatório:
> `docs/N2b2-RELATORIO.md`.

## 1. Conclusões consolidadas sobre a thread 3 (CRI)

Tudo medido no roteiro NEW GAME (área 1, 8 000 VBlanks) e na exploração (16 000), com o build
de `246e6b3`.

### 1.1 O que a thread 3 é

- É o laço `8C158840`, que chama a passada `8C1586B8`:
  - servidor ADX (`8C178874`);
  - 32 entradas MW (Sofdec);
  - GD-ROM;
  - gerenciador de streams (`8C174888`).
- Não tem ponto de espera: gira enquanto tiver a CPU.

### 1.2 Quem a faz rodar (todo por evento, no próprio jogo)

1. A cada render, a thread principal cede a vez a ela (`8C158864` → `8C17DDCC`).
2. O callback de VBlank (`8C158A26`) pede a volta para a thread principal.
3. Na exclusão mútua `8C1588AA`, a thread principal cede enquanto a thread 3 estiver no meio de
   uma passada (`[0x8C1DD1BC]` = 1), e a thread 3 devolve. São 4 cessões por rota.
   - A causa está provada troca a troca: a marcação na cessão `8C17DDCC` registra 8 de 8, todas
     com PR `8C1588C2` ou `8C158854`.

### 1.3 Quando ela está ociosa

| Situação | Evidência |
|---|---|
| **Gameplay e menus** (MW e ADX inativos) | O predicado barato funciona: nenhuma leitura em curso nem emitida, nenhum buffer esvaziado, nenhuma candidata, nenhuma mudança no gerenciador. Validado por um observador independente: em 6 janelas, nenhuma passada ociosa sem evento muda a RAM principal ou de som, exceto os medidores do TMU (e com a D7, nem eles) |
| **Vídeo** (MW ativo) | **Indício de ociosidade entre eventos, não prova.** No meio dos dois vídeos (V2150, V4550; também com o TMU parado), das ~1 005 passadas com MW ativo e sem evento, **todas** deixam a RAM principal e a de som inalteradas fora dos medidores. O observador não cobre VRAM, MMIO, FIFOs e store queues, nem o estado interno do GD-ROM e do runtime. O predicado barato conta toda passada com MW ativo como trabalho |
| Transições sem evento | 1 em 8 000 VBlanks (consumo de stream pelo relógio de software, V4649). Com o TMU parado por quadro: 0 |

### 1.4 Bloquear até o próximo evento (simulado)

- **Resultado:** com o conjunto completo de eventos de despertar, `pular5` dá TN idêntica sem
  normalização no roteiro da primeira área, em dois comprimentos (`newgame_area1` e
  `newgame_exploracao`), e também IRQF, leituras, estado, áudio e quadro iguais.
- **Eventos de despertar:**
  - interrupção aceita;
  - troca de thread;
  - VBlank;
  - evento do Holly, mesmo mascarado;
  - bloco de GD-ROM.
- **Limite:** o experimento só pula passadas sem MW/ADX ativos, porque o predicado do vídeo não
  existe.

## 2. Condições de espera que ainda impedem o N2b-2

No N2b-2 o tempo só anda quando o contexto corrente está num ponto de espera declarado
(`docs/N2b-PLANO.md`, 3.4). Inventário pelas janelas sem espera (aviso de falta de progresso com
o PC do maior lote, limite baixo como diagnóstico):

| # | Condição | Situação | Por que bloqueia |
|---|---|---|---|
| E1 | Thread 3 com MW/ADX ativo (vídeo) | **Bloqueia.** Ociosa entre eventos (1.3), mas sem predicado barato. As duas maiores janelas sem espera da rota são os vídeos inteiros: V2096–V2254 (158 VBlanks) e V4480–V4630 (150), com a thread 3 fazendo 99% do trabalho | Sem predicado, a thread 3 nunca bloqueia durante o vídeo; o tempo não anda e o vídeo não avança. A guarda da D8 dispara |
| E2 | Thread 3 com MW/ADX inativo | **Resolvida** (predicado do gerenciador de streams + época de eventos completa) | — |
| E3 | Relógio de software dos streams (TMU dentro do quadro) | **Resolvida com a D7** (TMU lógico parado dentro do passo) | Sem a D7, uma transição por rota acontece sem evento |
| E4 | Thread 0: janelas longas | As medidas são computação (`8C0100EE` zera o BSS no boot; `8C1C43C8` percorre dados). Durante o vídeo, a thread 0 fica suspensa (12 mil pontos de controle em 158 VBlanks) | — |
| **E9** | **Esperas temporais não declaradas (revisão do Codex)** | **Bloqueia.** Laços que esperam hardware com o tempo andando: `8C172624`/`8C17262A` (bit de vsync do `SPG_STATUS` ligar e desligar; já travou o boot em 2026-10-13) e `8C172804` (limpa o VBlank-In do `ISTNRM` e espera ele voltar). Polling de DMA em `8C1C1DC2` (`C2DST`): hoje o DMA conclui na própria escrita, mas um DMA assíncrono criaria outro bloqueio. Pode haver outros, alcançáveis por ponteiro ou sem cobertura | Com o tempo andando só nas esperas declaradas, esses laços nunca veem a transição |
| E5 | Esperas declaradas (VBlank `8C064A2C`, render `8C173086`, bit `8C0978F6`) | **Prontas** (modo sombra: todas liberadas pelo evento esperado) | — |
| E6 | Trabalho longo sem espera (boot V0–V34, cargas) | **Decisão pendente (D11, nova).** No N2b-2 o trabalho não consome tempo: o boot que hoje leva 34 VBlanks passaria em zero | Os oráculos por VBlank (TN, IRQF, CL com VBlank) deixam de valer nessas janelas. É preciso um oráculo por sequência de passos lógicos |
| E7 | GD-ROM legado com latência por tempo | **Decisão D9(a)** pendente para o provedor legado | B4 entre provedores |
| E8 | Áudio | Hoje nada consome os streams (sem ARM7). O estado do driver de som é **entrada da lógica** (muda o vídeo quando zerado) | Não bloqueia o N2b-2 com a referência atual, mas o áudio nativo vai mudar o comportamento |

> **Situação depois da investigação P0/P1 (seções 9 e 10):**
> - E1: ociosidade do Sofdec/ADX comprovada em **uma rota** (NEW GAME até a primeira área, em
>   dois comprimentos) pelo detector de efeito líquido, com as lacunas da 9.2. Falta o predicado
>   de produto (U5). Continua aberto.
> - E9: esperas executadas nessa rota classificadas (seis laços de espera no boot a declarar, em
>   V2 e V13), com os limites de completude de `docs/N2b-ESPERAS.md`. Fica pendente no U7
>   (outras rotas). Continua aberto.

**Bloqueios técnicos abertos: E1 (predicado do vídeo e observador completo) e E9 (inventário
fora da rota coberta e declaração das esperas temporais).** O E6 é uma decisão de produto, não
de equivalência.

## 3. Áudio e CRI nativos antes de eliminar os ciclos?

### 3.1 Opção A: N2b-2 primeiro (com predicado do MW/ADX)

**Prós:**
- A ociosidade da thread 3 já é por evento, inclusive no vídeo (1.3). O CRI nativo não é
  pré-requisito técnico.
- O N5 (vídeo) e o N6 (áudio) precisam de um relógio lógico com eventos (quadro de vídeo,
  consumo de áudio, `AudioTick`). Feitos antes, ficariam presos aos ciclos estimados e teriam de
  ser refeitos sobre o relógio lógico.
- As referências atuais (vídeo e áudio legados) continuam valendo como oráculo durante o N2b-2.

**Contras:**
- O predicado do MW/ADX (E1) é trabalho que o N5 vai aposentar.

### 3.2 Opção B: N5 e N6 primeiro (CRI e áudio nativos), depois N2b-2

**Prós:**
- A thread 3 perderia MW, ADX e streams de som. O laço do CRI poderia virar um provedor
  nativo, e o N2b-2 ficaria só com as esperas da thread 0.

**Contras:**
- **N5 e N6 são as maiores migrações do plano:** MPEG-1 e ADX, sequenciador do `BS_SND.MLT`,
  protocolo do Manatee.
- **Mudam o comportamento de referência:** o estado do driver de som decide o que a biblioteca
  faz e altera o vídeo. Com um driver que responde, a lógica muda, e os oráculos atuais (iguais
  ao legado) deixam de valer justamente quando o tempo ainda está em ciclos.
- **O relógio:** sobre ciclos estimados, o ritmo de consumo de áudio e a sincronia do vídeo
  ficariam amarrados a uma base que vai sair.

### 3.3 Opção C (recomendada): fechar o E1 por investigação e só depois o N2b-2

Sequência:
1. Fechar o E1 com um predicado do MW/ADX.
2. Fazer o N2b-2.
3. Fazer o N6 e o N5 sobre o relógio lógico.
4. Em paralelo, só como análise, levantar o protocolo do driver de som.

Por quê:
- O E1 é limitado. O observador de RAM indica que as passadas sem evento não fazem nada. Faltam
  um observador completo (VRAM, MMIO, filas, estado interno) e um predicado barato (seção 4, P1).
- O E9 é um inventário estático, com dinâmico de apoio (seção 4, P0).
- O N2b-2 entrega a base de tempo de que N5 e N6 precisam, e mantém os oráculos atuais.
- O protocolo do driver de som (bloco de estado `0x00813A00`, comandos) é a peça que o N6 precisa
  entender, e pode ser analisado sem mudar o produto.

## 4. Sequência proposta

Cada etapa só começa com autorização. Tudo atrás de chave (legado como padrão até os portões).

### P0. Inventário das esperas temporais (investigação)

- **Varredura estática** de todos os back-edges cujo laço lê regiões temporais ou de estado
  assíncrono:
  - SPG (`SPG_STATUS`, linhas);
  - `ISTNRM`, `ISTEXT` e `ISTERR`;
  - TMU, RTC;
  - estado de DMA (`C2DST`, `SB_*`), Maple, GD-ROM;
  - registradores e RAM de estado da AICA.
- Inclui código alcançado por ponteiro (o `bs-hwscan` já cruza os acessos com funções).
- **Dinâmico:** pontos de controle por back-edge nas rotas, com os laços acima marcados.
- **Saída:** cada laço classificado em:
  - espera real, a declarar com o evento que a libera (VBlank, linha do SPG, fim de DMA);
  - polling já concluído sincronamente, que fica registrado como dependência;
  - substituído no boot nativo (justificar).
- **Testes:** cada espera declarada nova tem o evento esperado entregue no episódio (S2); negativo:
  espera removida → falta de progresso com o PC.

### P1. Predicado de ociosidade do MW/ADX (investigação, diagnóstico)

Duas rotas possíveis, a escolher por evidência:
- **(a) Específico:** engenharia reversa do servidor MW/Sofdec e do ADX:
  - o que cada um consulta para decidir decodificar (quadro devido? buffer?);
  - um predicado como o do gerenciador de streams.
- **(b) Genérico:** detector conservador de efeitos da passada da thread 3.
  - Usa uma bandeira "passada da thread 3 ativa", não uma faixa de endereços de função, por
    causa das chamadas indiretas.
  - Observa escritas na RAM (com os espelhos normalizados), na VRAM, em MMIO, FIFOs e store
    queues, e também leituras com efeito (filas consumidas).
  - Distingue "escrita ocorreu" de "valor mudou".
  - Mantém exclusões explícitas: pilha morta, medidores, índice do rodízio.
  - **Limites:**
    - O resultado só existe depois da passada. Serve para bloquear a próxima, sob a hipótese
      "sem evento, continua ociosa", que precisa ser testada.
    - O custo não é só uma comparação por escrita: entram um desvio global, a normalização de
      endereço e, para detectar mudança, a leitura do valor anterior.
    - Medir no B9, também fora do vídeo.

**Testes:**
- um observador ampliado (RAM principal e de som, VRAM, escritas de MMIO/FIFO/SQ, estado do
  GD-ROM) vira o oráculo do predicado. O atual, só RAM, não basta;
- `pular5` estendido ao vídeo, com o controle `repetir5`;
- negativo: predicado que ignora o MW, detectado nas leituras e no quadro;
- rotas: NEW GAME, exploração e um roteiro com o vídeo completo.

**Risco:** o MW pode decidir por um relógio próprio (TMU). A D7 já cobre isso; testar com
`BS_TMU_DIAG=quadro`.

### P2. Decisões do mantenedor antes do N2b-2

| Decisão | O que é |
|---|---|
| D7 | TMU lógico, parado dentro do passo. Necessária: remove a única transição sem evento |
| D9(a) | Conclusão no próximo ponto de espera, também no GD-ROM legado (B4) |
| S4 redefinido | Artefatos lógicos, não chamadas por VBlank (`docs/N2b-BLOQUEIOS.md`, 2.7) |
| Exceção fechada | Cessões `8C1588AA` com causa marcada (`config/n2b_excecoes.toml`) |
| **D11 (nova)** | **Mudança de comportamento do produto, não equivalência:** trabalho longo sem espera não consome tempo lógico (cargas instantâneas). Ela só vale num trecho provado atômico perante o modelo lógico, ou seja, sem consumidores temporais alcançáveis (as esperas do P0 ficam fora). O portão continua comparando a sequência completa de eventos externos e o estado em checkpoints depois de cada carga. VBlanks não são removidos do oráculo sem essa prova. Consumidores de VBlank conhecidos: o escalonador do jogo (um passo por VBlank) e o callback do CRI `8C158A26`. A dependência de animações de carga não foi verificada |

### P3. N2b-2 (implementação, depois de P1 e P2)

- `BS_WAIT` passa a deixar o tempo andar até o próximo evento.
- A thread 3 ganha um ponto de espera condicional no back-edge `8C158860`: bloqueia quando a
  passada foi ociosa (predicado P1) e acorda no próximo evento da fila.
- Não é espera fictícia: o tempo só vai ao próximo evento real, e a escolha de thread continua
  com o despachante do SDK.
- `--tempo n2b` como opção, com o legado padrão até os portões.

**Portões:**
- trace com sequência igual ao legado (D11) nas três rotas;
- leituras na mesma ordem;
- estado por checkpoint;
- quadros dos checkpoints;
- AUD;
- zero faltas de progresso (D8 recalibrado nessa medição);
- desempenho;
- todas as suítes (Builder, Preview, mods, arquivos, ordem, tempo, N2b, CRI).

**Negativos:**
- espera removida;
- evento perdido;
- predicado errado;
- liveness (laço sem espera → erro com PC).

### P4. Protocolo do driver de som (análise, em paralelo com P1 a P3)

- Mapear o bloco de estado (`0x00813A00`, `0x00813A18`, `0x00813400`) e a fila de comandos do
  lado SH-4.
- O `MANATEE.DRV` entra só como documentação (análise estática, nunca executado).
- Saída: o oráculo de áudio em nível de API, ou seja, a sequência de comandos da biblioteca de
  som. Ele substitui o AUD quando o driver responder.

### P5. N6 (áudio nativo) e N5 (vídeo nativo), sobre o relógio lógico

- O áudio nativo responde no bloco de estado e consome os streams. O despertar da thread 3
  ganha o `AudioTick`.
- O vídeo nativo sincroniza pelo relógio do vídeo (plano do port).
- Com N5 e N6, o MW e o ADX saem da thread 3, e o predicado do P1 deixa de ser necessário.
- **Risco:** a referência de comportamento muda, porque a biblioteca passa a mandar comandos. Os
  oráculos têm de ser os de nível de API (P4) e a lógica de jogo fora do áudio.

### P6. Roteiros de cobertura (contínuo, antes do N2c)

- Gravador de entrada (`BS_INPUT_RECORD`) para gravar rotas jogando.
- Roteiros necessários:
  - vídeo completo;
  - troca de sala;
  - save/load;
  - combate.
- Usos: D8, o oráculo D11 e a cobertura do predicado.

## 5. Riscos

| Risco | Efeito | Mitigação |
|---|---|---|
| Predicado do MW incompleto | Thread 3 bloqueia com trabalho pendente: vídeo atrasado ou travado | Observador de RAM como oráculo; negativos; liveness |
| Registro de escrita (P1b) caro | Desempenho | Medir no B9; restringir às funções do CRI |
| D11 muda a temporização visível (cargas instantâneas, animações de carga que andam por VBlank) | Diferença visual em telas de carga; perda de eventos escondida pelo oráculo | Trace dedicado de consumidores de VBlank durante cargas. Portão com a sequência completa de eventos externos. Nada de VBlanks mínimos ou de qualquer avanço de tempo para ritmo visual dentro do N2b (seria espera fictícia); se o mantenedor quiser ritmo de apresentação, é outro modo, não equivalente e fora do relógio lógico do jogo |
| Esperas temporais não declaradas (E9) | Trava no boot ou em código sem cobertura | Inventário do P0; guarda de liveness com PC |
| O áudio nativo muda a lógica (estado do driver) | Oráculos atuais quebram | P4 antes do P5; oráculo por API |
| Uma rota só de gameplay | Condições não exercitadas (combate, salas) | P6 |
| Medidores lidos por base+deslocamento | Uma decisão por TMU passaria despercebida | Evidência dinâmica (TMU parado → lógica igual) em mais rotas (P6) |

## 6. Recomendação

1. **Próxima etapa: P0 e P1** (inventário das esperas temporais; observador ampliado e predicado
   do MW/ADX), só investigação e diagnóstico, com P4 em paralelo como análise.
2. Depois, as decisões do P2 (D7, D9(a), S4, a exceção e a nova **D11**) e o **N2b-2** (P3).
3. Áudio e vídeo nativos (P5) **depois** do N2b-2, sobre o relógio lógico.
4. A substituição do CRI não precisa vir antes da eliminação dos ciclos:
   - a alternância das threads já é movida por evento;
   - a ociosidade da thread 3 é comprovada fora do vídeo e indicada no vídeo.

   Fazê-la antes trocaria o oráculo de comportamento e o relógio ao mesmo tempo.

## 7. Revisão do Codex (`reviews/codex-20261016-163758.md`)

| Achado | Decisão | Ação |
|---|---|---|
| [ALTA] A ociosidade no vídeo não decorre do observador (só RAM principal e de som) | **Aceito** | 1.3 reformulada como indício; o P1 exige um observador ampliado (VRAM, MMIO/FIFO/SQ, GD-ROM) |
| [ALTA] O inventário omitia esperas temporais reais (`8C172624`/`8C17262A` SPG, `8C172804` ISTNRM) e o polling de DMA `8C1C1DC2` | **Aceito**, confirmado no disassembly | E9 e etapa P0 (inventário estático e dinâmico) |
| [ALTA] A D11 podia esconder perda de eventos | **Aceito** | D11 formulada como mudança de produto, só em trecho provado atômico, com a sequência completa de eventos no portão |
| [MÉDIA] P1(b): "nenhuma escrita" não basta e o custo foi subestimado | **Aceito** | P1(b) virou um detector conservador de efeitos, com limites e custo explícitos |
| [MÉDIA] VBlanks mínimos por carga seriam espera fictícia | **Aceito** | Removido do caminho equivalente |
| [BAIXA] Contadores do observador não zerados por sessão | **Aceito** | Zerados em `cri_diag_install` (alteração não commitada) |

## 8. Decisões do mantenedor (2026-10-16)

| Decisão | Teor |
|---|---|
| D7 | Relógio lógico aprovado como arquitetura-alvo, condicionado à validação de todos os consumidores relevantes do temporizador |
| D9(a) | Qualquer mudança na conclusão de controles e GD-ROM preserva os contratos já demonstrados e é validada **separadamente** nos provedores legado e nativo |
| S4 | A equivalência do servidor CRI considera trabalho efetivo, eventos, leituras e estados, não só a quantidade de chamadas |
| Cessões | Só trocas **comprovadamente** originadas pela primitiva de cessão recebem tratamento especial |
| D11 | Cargas instantâneas **não** autorizadas indiscriminadamente: identificar os trechos seguros e tratar os demais explicitamente |

## 9. Resultados da investigação P0 e P1 (2026-10-16, sem commit)

**Cobertura:** **um** roteiro (NEW GAME até a primeira área), medido em dois comprimentos (8 000 e
16 000 VBlanks). Não houve troca de sala, combate nem save/load. Todo "cumprido" abaixo vale só
para esse roteiro.

Ferramentas novas, todas de diagnóstico e desligadas por padrão:

| Ferramenta | O que faz |
|---|---|
| `tools/dev/n2b_esperas_estaticas.py`, `tools/dev/n2b_lacos_quentes.py` | Varredores do inventário; relatório auditável em `docs/N2b-ESPERAS.md` |
| Build de investigação `-DBS_MODS=ON -DBS_WRITE_WATCH=ON -DBS_BUILD_GAME=ON` | Nunca é o produto. Sonda no caminho de escrita e no `BS_POLL`: RAM com o valor original por palavra (mudança líquida), escritas fora da RAM (MMIO, VRAM, AICA), descarga de store queue (`pref` direto ao TA), escritas do host (`GuestMemory`), hash do estado do HLE do GD-ROM (inclui `xferDue`) e do TMU (inclui o UNF aceso por leitura do `TCNT`). `BS_WW_VOLTAS`: voltas exatas por back-edge (`BS_POLL`), com os retornos de chamada (`BS_POLL_RET`, do `BS_CALL`) contados à parte e as **voltas sem progresso** (estado arquitetural repetido no mesmo laço e nenhuma escrita). `BS_WW_DECLARAR`: as seis esperas do boot informadas ao monitor (modo sombra) |
| `BS_PROGRESSO_PCS` | Perfil por PC. Credita lotes ao PC do checkpoint, então é só indicativo; as voltas exatas substituem |
| `BS_CRI_DIAG=efeito`, `pular6`, `repetir6`, `pular6r`, `pular6s` | Efeito por passada; bloqueio simulado; controle; negativos (ignora a RAM / a store queue) |
| `tools/dist/test_efeito.ps1` | Critérios E1–E7, reproduzíveis |

### 9.1 Inventário das esperas temporais (P0)

Detalhes, método e motivo de cada classificação em **`docs/N2b-ESPERAS.md`**. Dados brutos com
proveniência em `analysis/n2b/`.

**Critério dinâmico:** a "volta sem progresso" é um back-edge no mesmo PC com registradores
inteiros, de FPU e MAC iguais, e sem nenhuma escrita entre as duas voltas. Um laço determinístico
nesse estado só sai por mudança externa. O critério vale também para laços que chamam funções.

Dos 833 laços executados, exatamente seis têm voltas sem progresso, e os seis são esperas
temporais.

| Classe | Laços |
|---|---|
| Esperas declaradas | `8C064A2C` (VBlank), `8C173086` (render), `8C0978F6` (0 voltas) |
| **Esperas temporais não declaradas** (todas no boot, uma vez) | V2: `8C172624`/`8C17262A` (vsync do SPG), `8C172804` (VBlank-In). V13: `8C1C6CB0`/`8C1C6D08` (linha de varredura do SPG voltar a 0, por um getter chamado por registrador; **achada pelo critério de progresso, invisível ao varredor estático**), `8C172EF2` (contador do callback de VBlank) |
| Polling satisfeito sincronamente (0 voltas) | `8C1C1DC2` (`C2DST`), `8C096EE0` e `8C09724C` (`SB_FFST`) |
| Substituída no provedor nativo | `8C15BC7E` (`gdFsStop`) |
| Computação | os outros 827 laços executados, nenhum com volta sem progresso. Os 68 sem escrita nem chamada também foram classificados pela estrutura (10 sem leitura, 41 por ponteiro móvel, 13 de endereço fixo revisados à mão) |
| Falsos positivos do estático (pool) | `8C08F668`, `8C090044` |

No N2b-2, as seis esperas do boot têm de ser declaradas com o evento real que as libera:
- **VBlank-In** para `8C172804` e `8C172EF2`;
- **bordas do vsync** para `8C172624` e `8C17262A`;
- **linha 0** para `8C1C6CB0` e `8C1C6D08`.

São instantes do relógio lógico pela configuração do SPG, e não avanço artificial.

**Fora do alcance do inventário:**
- esperas que mudam o estado a cada volta, como um contador de timeout ou uma função chamada que
  escreve: no N2b-2 saem por timeout, por outro caminho;
- esperas de 0 voltas fora do varredor estático;
- outras rotas.

### 9.2 Ociosidade do Sofdec e do ADX (P1)

**Detector de efeito líquido** (mesma rota):

| Medida | 8 000 VBlanks | 16 000 VBlanks |
|---|---|---|
| Passadas sem evento | 2 653 809 | 5 310 910 |
| Com efeito líquido | **81** | **81** |
| Sem efeito, regravando o mesmo valor (polling sem alteração de estado) | 2 564 118 | 5 221 219 |
| Sem efeito, com mudanças só transitórias | 89 610 | 89 610 |
| Com Sofdec/ADX ativos e sem evento: sem efeito | 89 610 de 89 661 | 89 610 de 89 661 |
| Violações (efeito sem evento depois de passada sem efeito) | **0** (também com o TMU parado) | **0** |
| Passadas com descarga de store queue | 4 015 (todas com outro efeito também) | — |
| Passadas com mudança de estado do TMU ou do GD-ROM | 0 | 0 |

**Trabalho útil × polling:**
- Útil é a passada com efeito líquido, e quase sempre há evento durante ela.
- Polling sem alteração de estado é a passada sem efeito líquido.
- Durante o vídeo, **toda** passada muda RAM transitoriamente (bandeiras acesas e desfeitas). Por
  isso a captura em janela e o registro de mudança imediata davam respostas opostas, e só a
  mudança líquida distingue as duas coisas.

**Supressão de passadas sob o modelo atual (`pular6`), inclusive com Sofdec/ADX ativos:**

| Comparação | 8 000 VBlanks | 16 000 VBlanks |
|---|---|---|
| TN, IRQF, CL, SL, AUD, quadro | **idênticos** ao legado | **idênticos** |
| Passadas executadas | 12 520 em vez de 2,67 M | 24 517 em vez de 5,34 M |
| Controle `repetir6` | idêntico | idêntico |
| Negativo `pular6r` (ignora a RAM) | TN, IRQF, CL, SL, AUD diferentes | TN, IRQF, CL, SL, AUD e quadro diferentes |
| Negativo `pular6s` (ignora a store queue) | **vazio nesta rota** (nenhuma passada tem efeito só por ela) | — |
| TMU parado (D7) + supressão | faltam só as 4 cessões marcadas causalmente; o resto é idêntico | idem |

**Limite (revisão do Codex):**
- Isto prova que **suprimir** as passadas sem efeito, com o tempo ainda andando por ciclos, não
  muda os artefatos.
- **Não** é o bloqueio real. No N2b-2, o tempo anda até o próximo evento e o evento é entregue
  antes da retomada. A equivalência do bloqueio real só se prova no N2b-2 experimental, atrás de
  chave.

**Efeitos que o detector ainda não vê:**
- leituras com efeito além do `TCNT` (p.ex. filas FIFO lidas);
- estado interno do runtime fora do GD-ROM e do TMU (fila de eventos, Maple, PVR);
- escritas de DMA iniciado antes da passada.

Nenhum desses aparece como violação nesta rota, mas não está provado em geral.

### 9.3 Trechos de trabalho longo (D11)

Com a supressão informada ao monitor como espera e as seis esperas do boot declaradas em modo
sombra (`BS_WW_DECLARAR`), as janelas sem espera passam a medir trabalho real:

| Trecho | VBlanks | Thread | Trabalho (pontos de controle) | Natureza |
|---|---|---|---|---|
| Boot V0–V2 | 2 | 0 | 1,29 M | inicialização, até a espera do vsync |
| Boot V2–V13 | 11 | 0 | 4,20 M | inicialização, até a espera da linha 0 do SPG |
| Boot V13–V33 | 20 | 0 | 9,72 M | inicialização (BSS, dados) |
| V225–V227 | 2 | 0 | 0,67 M | menus |
| V2096–V2127 | ~25 em 3 janelas | 3 | 0,43–0,53 M cada | início do vídeo: decodificação e enchimento |
| V2255–V2264 | 9 | 0 | 2,1 M | carga depois do vídeo |
| V4474–V4476 | 2 | 0 | 0,80 M | início do vídeo `E001` |
| V4631–V4639 | 8 | 0 | 1,8 M | carga da área |
| V6035–V6037 | 2 | 0 | 0,69 M | gameplay |
| V6249–V6251 | 2 | 0 | 0,74 M | modelos de item do menu |

Cada trecho exige uma de duas coisas (D11):
- **prova de segurança:** consumidores de VBlank sem efeito lido depois por quem mede o tempo
  passado;
- **tratamento explícito** como mudança de comportamento.

Não dá para medir a compressão sem o N2b-2 experimental. A proposta é comparar o estado em
checkpoints logo depois de cada trecho.

### 9.4 Cobertura além da primeira área

- Mais três variantes de roteiro (giros e ação na porta do C-GATE) não chegaram a outra sala:
  nenhum arquivo novo foi lido depois da leitura 6300.
- Sem um gravador de entrada (`BS_INPUT_RECORD`, P6) ou uma rota fornecida pelo mantenedor, a
  cobertura continua a primeira área.

### 9.5 Custo da instrumentação

Ver a seção 12 (medição sequencial).

## 10. Critérios objetivos para desbloquear o N2b-2

| # | Critério | Situação |
|---|---|---|
| U1 | Inventário: toda espera temporal executada nas rotas de cobertura classificada; as reais com evento definido | **Parcial.** Na rota coberta: os 833 laços executados, todos testados pela volta sem progresso (6 esperas, 4 delas não declaradas) e cruzados sem corte de tamanho nem de frequência, com os dados brutos e a proveniência em `analysis/n2b/` (`docs/N2b-ESPERAS.md`). Lacunas: esperas que mudam estado a cada volta (contador de timeout) entre os 765 laços com escrita ou chamada, esperas de 0 voltas fora do varredor estático, laços fechados por desvio indireto, e outras rotas (U7) |
| U2 | Ociosidade da thread 3: efeito líquido com 0 violações, com e sem o TMU parado, inclusive com Sofdec/ADX | **Cumprido em uma rota**, com as lacunas do detector da 9.2 |
| U3 | Supressão de passadas sem efeito sob o modelo atual = legado, controle igual, negativo detectado (`test_efeito.ps1`) | **Cumprido em uma rota**. O bloqueio real é o portão do N2b-2 experimental |
| U4 | D7: TMU parado + supressão = legado, exceto as cessões marcadas causalmente; consumidores do TMU validados (B11) | **Cumprido em uma rota**. "Todos os consumidores relevantes" depende do U7 |
| U5 | Predicado de produto com custo dentro do B9 (≤ 1,05×) e o detector como oráculo | **Pendente.** Seção 12: só há uma extrapolação linear, sem medida do mecanismo real |
| U6 | D9(a): conclusão de controles e GD-ROM validada separadamente no legado e no nativo | **Pendente** |
| U7 | Cobertura: ao menos uma rota reproduzível com troca de sala e uma com save/load, com U1–U4 repetidos | **Parcial (2026-10-23).** As rotas existem: duas sessões gravadas do mantenedor (troca de sala, combate, save e LOAD) e as rotas do `test_assistido`; o estado por passo do experimental é igual ao do estável fora da exceção `d11-carga-cartao` (`docs/N2-PLANO.md` 13.3). Falta repetir U1–U4 (inventário e detector) nelas |
| U8 | D11: cada trecho da 9.3 provado seguro ou tratado explicitamente | **Pendente** (exige o N2b-2 experimental) |
| U9 | Revisão independente do Codex | Seção 11 |

## 11. Revisões do Codex desta investigação

**`reviews/codex-20261016-182653.md`:** todos os achados aceitos.

| Achado | Ação |
|---|---|
| [ALTA] Detector com falsos negativos (`pref` direto ao TA, `TCR.UNF` aceso por leitura, `xferDue`) | Descarga de store queue, hash do TMU e `xferDue` no detector. Contagens: 4 015 passadas com descarga (nenhuma só com ela), 0 com o TMU ou o GD-ROM. Negativo `pular6s`, declarado vazio nesta rota. Lacunas restantes na 9.2 |
| [MÉDIA] Inventário não auditável; perfil por lote | Varredores em `tools/dev/`, relatório `docs/N2b-ESPERAS.md`, voltas exatas por back-edge (`BS_WW_VOLTAS`) |
| [MÉDIA] `pular6` não é o bloqueio real | U3 reescrito como supressão sob o modelo atual; o bloqueio real é o portão do N2b-2 experimental |
| [MÉDIA] O boot V0–V33 continha as próprias esperas | Esperas declaradas em modo sombra; boot dividido em V0–V2, V2–V13, V13–V33 (9.3) |
| [MÉDIA] "Duas rotas" era um roteiro em dois comprimentos | Texto e critérios: "cumprido em uma rota" |
| [BAIXA] Contrato de duração da mídia moddável contraditório | `PLANO-PORT-NATIVO.md` 4.1: modo fiel × modo modificado |

**`reviews/codex-20261016-194556.md`** (revisão final): todos os achados aceitos.

| Achado | Ação |
|---|---|
| [MÉDIA] O resumo da seção 2 e a 1.4 ainda diziam "duas rotas"; E9 aparecia como concluído e como aberto | Texto corrigido para uma rota em dois comprimentos; E1 e E9 continuam abertos |
| [MÉDIA] U1 apoiado em busca heurística (cortes de 40/96 bytes e de 5 000 voltas); TSV bruto sem proveniência | Cruzamento de **todos** os back-edges executados, sem corte. Novo critério da volta sem progresso, que vale também para laços com chamada. Classificador estrutural com revisão manual. Dados brutos, comandos e hashes em `analysis/n2b/`. U1 passa a "parcial", com as lacunas listadas. **O novo critério achou uma quinta espera não declarada** (`8C1C6D08`, linha 0 do SPG por um getter) |
| [MÉDIA] `BS_WW_VOLTAS` misturava voltas e retornos de chamada (`BS_CALL` também faz poll) | `BS_POLL_RET` separado do `BS_POLL`, com expansão idêntica fora do build de investigação. Retornos numa coluna própria. Para as esperas publicadas, a coluna de retornos é 0, e as contagens não mudaram |
| [BAIXA] E7 vazio contado como aprovado | E7 vazio vira `[SKIP]` explícito, e o teste exige `pular6s = pular6` nessa situação |
| [BAIXA] "< 0,1%" dependia de linearidade não medida | Rebaixado a "extrapolação linear", com o que falta medir; U5 continua pendente |

Correções desta rodada que vieram da própria verificação:
- **Delay slot no corpo do laço.** O cruzador incluía a instrução seguinte ao back-edge mesmo sem
  delay slot. Isso podia tirar um laço da lista "sem escrita" por uma instrução de fora (a
  direção perigosa). Corrigido: com isso, a espera de VBlank `8C064A26` entrou corretamente no
  controle positivo do classificador.
- **FPU no estado do laço.** A primeira versão da volta sem progresso ignorava os registradores
  de FPU e acusava `8C054A26` (passo em ponto flutuante). Corrigido.

**`reviews/codex-20261016-204336.md`** (verificação das correções): todos os achados aceitos.

| Achado | Ação |
|---|---|
| [MÉDIA] O estado comparado omitia `VBR`, `SSR`, `SPC`, `SGR`, `DBR`; a exclusão da pilha é presumida | Estado arquitetural completo. A exclusão da pilha fica declarada como hipótese que só pode gerar falso positivo, e as seis detecções foram conferidas à mão |
| [MÉDIA] Um só `Last` global: um laço externo com outro laço na volta nunca era comparado; a identidade era só o alvo | Tabela dos 16 laços ativos, cada um com estado, janela de pilha e marca de escrita próprios. O texto agora diz "804 inícios de laço, 833 back-edges associados". **Resultado idêntico:** os mesmos seis laços, os mesmos TSV byte a byte |
| [MÉDIA] Proveniência apontava um executável que não gerou os TSV | Refeito com o binário final; registrados o hash de `fontes.patch` (`git diff HEAD`) e de cada arquivo novo, além dos hashes dos TSV |
| [BAIXA] E7 vazio passava com artefato vazio | Usa `Same`: falha se algum artefato for diferente ou vazio |
| [BAIXA] "Sem custo" no produto era forte demais | Seção 12: "sem sonda no caminho quente", com o que sobra listado |

## 12. Custo da instrumentação (produto × diagnóstico)

Medida sequencial e alternada, 2 repetições, área 1, 3 000 VBlanks (ms de trabalho por campo):

| Configuração | Rep. 1 | Rep. 2 | Leitura |
|---|---|---|---|
| A produto (Release, sem mods) | 17,12 | 17,15 | referência |
| B build com mods | 17,19 | 17,20 | +0,4% (slots) |
| C investigação, sonda compilada e desligada | 17,71 | 17,76 | **+3,1% sobre B**: a verificação em toda escrita |
| D investigação, só o invólucro (`contar`) | 17,69 | 17,76 | ≈ C |
| E investigação, detector completo ligado (`efeito`) | 19,40 | 19,34 | +9,3% sobre D, com 2,67 M passadas |
| F detector + supressão (`pular6`) | 33,71 | 34,01 | artefato da simulação: sem bloqueio real, a thread 3 gira o laço vazio |

**Custo de diagnóstico:**
- C a F, só no build de investigação;
- no produto (A), não há sonda no caminho quente: as verificações por escrita e por poll são
  removidas por `#if BS_WRITE_WATCH`;
- o custo não é literalmente nulo: ficam a estrutura global `g_ww` (com um `unordered_map`), sua
  inicialização e destruição, `ww_poll_note` (nunca chamada) e os mapas de perfil do adaptador de
  tempo. Para custo nulo, essas partes teriam de ser compiladas só sob `BS_WRITE_WATCH`.

**Custo necessário a um predicado de produto (U5):**
- **verificação compilada nas escritas:** +3,1% se for em todas, o que consome a maior parte da
  margem do B9 (5%). Alternativa: compilar a verificação só nas funções alcançáveis pela passada
  da thread 3 (opção do gerador), deixando o resto do jogo sem custo;
- **detecção ligada só nas passadas da thread 3:** com o bloqueio, as passadas caem de 2,67 M
  para cerca de 12,5 mil (−99,5%). Uma **extrapolação linear** dos +9,3% daria menos de 0,1%.
  Isto **não** é uma estimativa fundamentada:
  - não separa o custo fixo (instalação do observador, hashes do GD-ROM e do TMU) do custo por
    passada;
  - não inclui o custo do próprio bloqueio;
  - a única medida com supressão (F) é mais lenta, por artefato da simulação.

  O U5 continua pendente até medir o N2b-2 experimental com o predicado de fato proposto.
