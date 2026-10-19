# N3-2: provedor nativo do SDK de controles (`pd`)

Data: 2026-10-18. Autorização do mantenedor: **N3-2 apenas** (plano em `docs/N3-PLANO.md`, seção 4).
Base: commit `e16e42f` (N3-0 e N3-1). O padrão continua `--controles legado`; nada do N3-3, N2c ou
N4–N9 foi iniciado.

## 1. Caminhos

```
legado (padrao):  InputSystem -> ponte -> Maple (GET_CONDITION/DMA) -> camada per -> servidor pd 8C15C4E6 -> PDS -> jogo
nativo:           InputSystem -> transitional/pad_adapter --------------------------------------------> PDS -> jogo
                  (a camada per continua sondando as portas para a VMU ate o N4; o Maple nao tem controle)
```

`--controles legado|nativo` (`app/main_sdl.cpp`, `GameOptions::controls`). No modo nativo:
- o Maple fica sem controle (`maple_set_controller_present(false)`, como `BS_MAPLE_PORTS=none`) e sem fonte
  de entrada (a ponte do N3-1 não é instalada);
- três slots novos em `config/bluestinger_us.toml` (`[native] slots`), trocados pelo `BoundaryRegistry`:

| Endereço | Função original | Provedor nativo |
|---|---|---|
| `8C0975C0` | disparo do DMA da camada `per` no VBlank | o original roda (sondagem das portas, VMU); se ele disparou (r0 ≥ 0), o adaptador tira a amostra: é o instante do GET_CONDITION legado |
| `8C15C4E6` | servidor `pd` (24 `PDS_PERIPHERAL`) | escreve as 24 unidades pelo contrato do N3-0 (`pad_contract::server_unit`) a partir do modelo da porta A |
| `8C15C668` | `pdGetPeripheral(porta)` | `8C2D8970 + exts.w(muls.w(r4, 52))`, com MACL e a palavra abaixo da pilha, como o original |

O init `8C15C10C` não é substituído: ele inicializa a camada `per`/Maple, que a VMU usa até o N4.

## 2. Contrato preservado

- `old` lido da própria RAM antes de cada passada, com as escritas do jogo (`8C064878`, `8C04E440`);
  `on`/`off`/`press`/`release` pelo modo do servidor (`8C2D8EB0`); gatilhos (bit digital com byte ≥ 128),
  analógicos (byte − 128), `support`/`id` do DEVICE_INFO (`000F06FE`), `name`, `extend`, a tabela de estado
  (`8C2D8E50`).
- As outras 23 unidades apontam para os buffers da camada `per` (`per_info(i) = *(*(8C097AAC)) +
  (int16)*(8C097AA2) + 120·i`, a mesma conta de `8C0979FA`), como no legado.
- **Info da porta A.** No legado o PDS aponta sempre para o buffer `8C2CFFA8` da camada `per`, e o
  **conteúdo** dele muda quando a camada processa as respostas: info do controle depois da identificação
  (DEVICE_INFO), "(none)" quando a porta fica vazia. Medido: com a porta A vazia no Maple, a camada `per`
  escreve "(none)" nesse buffer, então ele não pode receber a info sintetizada. No nativo o ponteiro é fixo
  na área nativa do BIOS HLE (seção 3) e o conteúdo segue o modelo: info do DEVICE_INFO legado (sintetizada,
  `pad_contract::controller_info`) com o controle identificado; senão, a cópia do que a camada `per`
  escreve para a porta A vazia.

### 2.1 Presença e amostra (modelo da camada `per`, sem protocolo)

Medido no legado e reproduzido sobre as amostras do `InputSystem`:
- a camada só lê um controle depois de identificá-lo (DEVICE_INFO); uma leitura sem resposta o perde e
  ela volta a identificar;
- o resultado de um pedido só vale depois que a camada o processa, no fim do DMA. O servidor do passo
  roda logo após o VBlank, antes disso, e por isso vê a amostra do disparo **anterior**;
- o índice das amostras (`reads`) é o número de GET_CONDITION respondidos no legado: roteiros
  "indice leitura" valem igual, e a coluna `leituras` do `BS_PAD_TRACE` bate.

**Onde o fim do DMA cai depende do provedor de tempo** (medido com dumps de RAM a cada VBlank em volta
de uma desconexão, `passo_conexao`):
- tempo por ciclos (`--tempo nativo` e `legado`): ~29 mil ciclos depois do disparo, no meio do passo; o
  jogo lê a info já atualizada no resto dele (info+0 = 0 no VBlank 16 407→16 408, um VBlank antes da
  coleta seguinte). Regra: coletar logo depois da primeira passada do servidor depois do disparo;
- tempo experimental (o instante só anda nas esperas): na espera seguinte, depois da lógica do passo.
  Regra: coletar no disparo seguinte.

É a única dependência do modo de tempo no adaptador; o N2c a substitui pela fronteira de quadro nativa.

### 2.2 Disparos sem DMA

`8C0975C0` volta sem disparar com código negativo (estado da camada ≠ 1: −3; transferência em curso: −1;
trava `tas.b`: −2). Medido: 18 925 entradas para 18 923 DMAs em `passo_cgate`. A amostra só é tirada
quando o retorno é ≥ 0; entre a escrita de MDST e o retorno não roda código do jogo.

## 3. Área nativa do BIOS HLE (reserva formal)

`runtime/hle/bios.h`: `kHleNativeAreaBase = 0x8C004000`, 0x100 bytes; `kHleNativePadInfo` = 0x78 bytes
para a info da porta A.
- **Dono:** a área do sistema `0x8C000000–0x8C007FFF` é do BIOS no mapa do Dreamcast; aqui o boot
  nativo a monta (0xFF mais as variáveis, vetores, stubs e a tabela `8C003174` medidos) e o BIOS HLE é o
  dono. A reserva fica longe de tudo o que o HLE define (unitário `pad_adapter_native_area_guard`).
- **O jogo não a usa:** em 11 dumps de gameplay (7 do N3-0 e 4 novos) os únicos bytes ≠ 0xFF da área
  são os do HLE; nenhum literal alinhado do `1ST_READ.BIN` aponta para `8C001010–8C007FFF` fora das
  estruturas do HLE (varredura em todos os espelhos P0–P3).
- **Guarda em execução:** na primeira escrita a faixa tem de estar em 0xFF; antes de cada escrita
  seguinte, ela tem de conter o que o adaptador escreveu. Qualquer outra coisa é erro fatal com o
  endereço.
- **Teste:** P7 (seção 5).

## 4. Independência funcional do Maple (evidências)

**Dinâmica** (`BS_PAD_FECHAMENTO`: entradas no código gerado por `BS_FN_ENTER` mais os contadores do
Maple; `passo_cgate`, tempo estável):

| Contador | legado | nativo |
|---|---|---|
| servidor `pd` `8C15C4E6` | 9 455 | **0** |
| decodificador `8C15C24C` / `8C15C3D4` / estado `8C15C490` | 9 455 / 9 455 / 226 920 | **0 / 0 / 0** |
| getter `8C15C668` | 9 440 | **0** (o nativo atende 9 440) |
| limpeza `8C15C1A2` | 28 389 | 24 = só o init original |
| teclado `8C17B60C` | 0 | 0 |
| GET_CONDITION da porta A | 18 922 | **0** |
| respostas do controle no Maple | 18 923 | **0** |
| leituras da fonte de entrada pelo Maple | 18 923 | **0** |
| DEVICE_INFO da porta A (sondagem sem resposta, como B–D) | 1 | 18 923 |
| DMAs da camada `per` | 18 923 | 18 923 |
| acessores da camada `per` (`8C08F4B6`/`8C08F4CC`) | 378 264 / 378 795 | 151 344 / 151 875 |

O que continua rodando no nativo é a camada `per` sondando as quatro portas (8 acessos por DMA nos
dois modos; o legado tem 24 × 9 455 a mais, que eram do servidor) e o init/encerramento originais do
`pd` (`8C15C10C`, `8C15C686`), mantidos de propósito para a VMU até o N4. Nenhum quadro de controle,
nenhuma resposta e nenhuma leitura da fonte passam pelo Maple. A suíte cobra isso em todas as execuções
nativas (P2), e o negativo `maple` (controle deixado no Maple com a ponte) é acusado.

**Estática** (P8): depois da regeneração com os três slots, o C++ gerado tem só `BSG_SLOT` nas duas
chamadas a `8C0975C0` e na única a `8C15C4E6`; nenhuma chamada direta a `8C15C4E6`, `8C15C668` ou
`8C0975C0`. O getter só é chamado por registrador (`8C064878`), e o despacho consulta o slot. A
regeneração mudou só isso (3 slots em `dispatch.cpp`/`funcs.h` e 3 pontos de chamada).

## 5. Testes

**Unitários** (`tests/unit/test_pad_adapter.cpp`, 5 casos; 234 no total):
- modelo de presença e servidor: boot, identificação, leitura, `old` da RAM, desconexão com botão, reconexão;
- coleta no tempo experimental;
- getter;
- guarda da área nativa e a posição da reserva;
- mutações: `old_host` só difere quando o jogo reescreve `on`; os demais mudam o que o servidor escreve.

**Integração** (`tools/dist/test_pd_nativo.ps1`, P1–P8; resultados na seção 6).

Durante o desenvolvimento, cada correção foi conferida com execuções diretas legado × nativo
(`passo_cgate`, conexão, boot, soft reset, matriz, transição), nos dois modos de tempo. Três defeitos
apareceram e foram corrigidos, todos com evidência:
1. amostra na entrada de `8C0975C0` contava 2 chamadas sem DMA (coluna `leituras` 2 à frente) → amostra só com retorno ≥ 0;
2. info com ponteiro trocado só no servidor → ponteiro fixo e conteúdo atualizado na coleta (boot: `info` no passo 61);
3. coleta no disparo seguinte → depois do servidor no tempo estável (desconexão: `acao0` no passo 8022; dumps de RAM a cada VBlank).

## 6. Resultados (código final, 2026-10-18)

| Suíte | Resultado |
|---|---|
| unitários | 235 (229 + 6 do adaptador) |
| `ctest` com disco | 100% (12; `boot_trace` pulado por projeto) |
| `test_pd_nativo` | **76/0** |
| `test_controles` (legado) | 39/0; K13: quadros e leituras do padrão iguais ao executável de `3a3fc92` |
| `test_assistido` (legado, dois modos de tempo) | 28/0 |
| `make_builder`, `make_preview`; `test_builder`, `test_projeto`, `test_preview`, `test_gameplay` | ok; 18/0, 13/0 (projeto exportado recompilado sem o CDI, com os slots novos e o adaptador), 9/0, 4/0 |

`test_pd_nativo` em detalhe:
- **P1** (10/10): legado = nativo nas 5 rotas, `--tempo nativo` e `experimental`. Iguais em quadros por
  passo, leituras de arquivo, cargas de sala, estado lógico, áudio e traço do PDS com as colunas de tempo
  (`vblank` e `leituras`). Passos comparados por rota (estável): `passo_cgate` 9 288,
  `passo_heliporto` 14 329, `passo_combate` 12 984, `passo_load_opcoes` 3 201 (LOAD GAME sem VMU e
  OPTIONS), `newgame_area1` 3 470 (roteiro "indice leitura": o índice de amostras do nativo = GET_CONDITION
  do legado).
- **P2** (20/20): fechamento em todas as execuções nativas; controle: o legado usa servidor e Maple.
- **P3** (30/30): conexão com A, porta vazia desde o boot, soft reset, limiares, matriz de 256 valores e
  transição injetada, nos dois modos: traço com tempo e quadros iguais, fechamento; conferências
  semânticas `pad_traco.py` (conexão, boot, limiares no nativo; soft reset no legado e no nativo).
- **P4**: duas execuções do nativo iguais. **P5**: build com mods (todos os slots) = release.
- **P6** (7/7): `press_on` (2 618 passos diferentes), `release_zero` (123), `amostra_adiantada` (1 025),
  `info_sem_conectado` (9 557), `presenca_imediata` (732), `maple` (fechamento acusa GET_CONDITION 1 347 e
  respostas 1 348). Controle `old_host`: traço igual, porque o jogo não reescreve `on` com controle
  compatível nas rotas; a sensibilidade de `old` está no unitário.
- **P7** (5/5): área nativa livre (0xFF) no fim das 5 rotas no legado; info do nativo = buffer da camada
  `per` do legado, nos 0x78 bytes.
- **P8**: estático ok.

## 7. Revisão do Codex

**1ª rodada** (`reviews/codex-20261018-182417.md`):

| Achado | Decisão |
|---|---|
| [MÉDIA] a coleta não é amarrada ao fim real do DMA (`g_due`) | **aceito em parte.** O fim exato não é reproduzível sem emular o tempo do barramento do controle (no nativo o DMA não leva quadros de controle e termina ~9 600 ciclos antes), o que o escopo proíbe. A regra do adaptador reproduz o legado em todas as rotas e cenários medidos (P1, P3). Entrou o invariante do caso que a regra não cobre (duas passadas do servidor entre dois disparos efetivos): contado (`pd_servidores_extras`) e cobrado em toda execução nativa (P2), 0 em todas. Limitação residual na seção 8. |
| [MÉDIA] o servidor nativo forçava resposta 0xFF e estado 0 nas outras 23 unidades | **aceito e corrigido**: elas usam a descrição bruta da camada `per` (`8C08F4B6`), como o original; teclado classificado é erro fatal (o runtime não o tem); unitário com FC e resposta desconhecida |
| [BAIXA] o soft reset não tinha a conferência semântica | **aceito**: P3 roda `pad_traco.py softreset` no legado e no nativo, com o controle sem START |
| [BAIXA] artefato ausente terminava com sucesso | **aceito**: erro (código 2) |

Também da releitura: o negativo `old_host` valia para as 24 unidades (só deve valer para a A0); e o
critério de `8C15C1A2` ficou exato: 24 × (init `8C15C10C` + encerramento `8C15C686`), porque o soft reset
chama o encerramento original do `pd` (2 falhas na primeira execução da suíte, critério corrigido).

**2ª rodada** `reviews/codex-20261018-220359.md`: confirma as quatro correções. [MÉDIA] a janela temporal residual continua; aceita como limitação explícita (seção 8), sem afirmar equivalência temporal completa. [BAIXA] o unitário novo só cobria subunidades: aceito, `pad_adapter_other_port_base_unit` decodifica a B0 pela descrição bruta (erro 0 e resposta 8; com erro, sem dispositivo; teclado = erro fatal); 235 unitários.

## 8. Limitações, riscos e pendências

- **Coleta:** o instante em que a identificação ou a perda do controle passa a valer é "logo depois do
  servidor" (tempo por ciclos) ou "no disparo seguinte" (experimental), e não o fim do DMA, uns 0,15 ms
  depois do disparo. Código que leia a info nessa janela veria a mudança cedo; nada nas rotas medidas faz
  isso. O N2c troca o marcador pela fronteira de quadro nativa.
- **Dependência do modo de tempo** na coleta (seção 2.1): é o que o próprio legado faz em cada provedor.
- **VMU, subunidades e teclado Dreamcast:** o runtime não os tem. As unidades ≠ A0 seguem a camada `per`,
  e o teclado é erro explícito. O N4 trata a VMU.
- **Área nativa do BIOS HLE:** reserva conferida em dumps, por literais e pela guarda em execução. Um
  caminho do jogo nunca exercitado que escrevesse ali dispararia a guarda (erro alto, nunca corrupção
  silenciosa).
- **Soft reset:** termina com erro nos dois modos (syscall do menu do BIOS sem HLE; achado do N3-0).
- **Hardware real:** controles físicos Xbox/PlayStation continuam pendentes de teste manual (aceitação do
  N3 completo).
- **Padrão inalterado:** `--controles legado`; o nativo é opcional e está em validação.
