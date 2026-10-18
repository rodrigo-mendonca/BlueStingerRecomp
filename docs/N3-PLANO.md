# N3: controles nativos (investigação, arquitetura e plano)

Data: 2026-10-17. Autorização do mantenedor: **só investigação e planejamento** do N3. Nada foi
implementado. Base: commit `3a3fc92` (gameplay assistido). Plano-mestre:
`docs/PLANO-PORT-NATIVO.md` (N3: "provedor nativo da API pad/per, sem Maple").

Restrições respeitadas: `--tempo nativo` continua o padrão; escalonador intocado; nenhuma camada
nova de emulação; N2c e N4–N9 não iniciados.

## 1. Método da investigação

| Medição | Como | Resultado |
|---|---|---|
| Acessos aos registradores Maple | `analysis/hwscan_sonda` (sonda de hardware do N0, rota até a área 1), reaproveitada | 5 funções do SDK; nenhuma do jogo (seção 2.3) |
| Protocolo efetivo | `BS_MAPLE_DEBUG=1`, sessão assistida com `passo_cgate.txt` até o passo 7 972 (16 309 VBlanks) | tabela 2.2 |
| Estruturas na RAM | 7 dumps de RAM (`dump`) com botões controlados (A, B+UP, soltos) e diferença por correlação | `PDS_PERIPHERAL` do SDK em `8C2D8970`; palavra de ações do jogo em `8C24C5D0` |
| Semântica dos campos | leituras `ram` com UP, ANALOG_LEFT, LT+RT, solto | seção 2.4 |
| Produtores e consumidores | literais no `1ST_READ.BIN`, `callgraph.tsv`, `bs-disasm`, rastreio de registrador nas 28 funções do jogo que usam o ponteiro | seções 2.4–2.6 |
| VMU sem o controle simulado | boot headless 1 500 VBlanks com e sem `BS_MAPLE_PORTS=none` | 14 quadros idênticos ("VMU not found") |

A sonda da sessão interrompida (`probe_saida.txt`) só registrou o início do boot, sem dados úteis.
Por isso as medições acima foram refeitas.

Artefatos (privados, `analysis/n3/`, fora do git), para auditar e repetir:
- `hashes.txt`: executáveis, `1ST_READ.BIN`, dumps e quadros, com SHA-256 e os comandos exatos;
- `dumps/a0..u2.bin` e `respostas.txt`: a sequência de comandos e a leitura de cada `ram`;
- `maple_comandos.tsv`: contagem por porta e comando na sessão inteira (até o passo 8 002, 16 350
  VBlanks);
- `boot_com/`, `boot_sem/`, `boot_mosaico.png`;
- `scripts/`: varreduras de correlação, literais e rastreio de campos.

Essas medições são da investigação. No N3-0 elas viram linha de base reproduzível, gerada por
ferramenta.

## 2. Fluxo atual da entrada

```
Windows                          host (runtime/input)              runtime (legado, HLE)
------------------------------   -------------------------------   ----------------------------------
teclado -> eventos SDL3 da       KeyEventInput (mapa BOTAO=TECLA)
janela                    ----->  + XInput (1o controle)       ---> maple.cpp: no MDST=1, runDma()
XInput (GetState)                 = make_combined_input            le a tabela DMA da RAM guest,
roteiros (testes)                 InputSource::read(porta, seq)     responde DEVINFO/GET_COND e grava
                                                                   a condicao (ativa em 0) no buffer
                                                                   de recepcao; agenda fim de DMA
                                                                   (100 ciclos/bit) -> ISTNRM
                                                                         |
guest: SDK Shinobi (camada Maple "per")                                  v
  VBlank -> 8C08EFF8 -> 8C08F332 (le MDST em 8C0976DE, recolhe respostas)
                     -> 8C0975C0 (monta 24 descritores x 44 B, buffer duplo; MDSTAR, MDST=1)
  acesso por unidade: 8C08F4B6(i) condicao bruta, 8C08F4CC(i) info do dispositivo
guest: SDK Shinobi "pd 1.07a"
  passo de jogo -> 8C173044 (servidor por quadro) -> 8C15C4E6 (servidor pd)
     para i = 0..23 (4 portas x 6 unidades): PDS_PERIPHERAL em 8C2D8970 + 0x34*i
     decodifica controle em 8C15C24C; outro dispositivo -> 8C17B60C; ausente -> 8C15C1A2
  8C15C668 = pdGetPeripheral(porta)
guest: jogo
  8C064A20 (laco principal, espera 8C064A2C) -> 8C064A52 -> 8C173044 -> 8C064878
     8C064878: escolhe a porta, guarda PDS* em 8C24CCB0, pos-processa a estrutura do SDK
  8C03AC2E: palavra de acoes do jogo (8C24C5D0) com auto-repeticao
  28 funcoes do jogo leem on/off/press/x1/y1/info pelo ponteiro 8C24CCB0
```

### 2.1 Ponto de amostragem e latência
A amostra não é tomada na entrega do VBlank-In. O instante é a escrita `MDST = 1` dentro do
tratador de VBlank do SDK:
- `8C08EFF8` chama primeiro `8C08F332` (`8C08F27C`) e depois `8C0975C0` (`8C08F282`);
- o `runDma()` do runtime chama `InputSource::read` nessa escrita.

A amostra é consumida pelo servidor `pd` no passo de jogo seguinte. Na área 1, o jogo roda a 2
VBlanks por passo (medido: VBlank 16 309 → 16 369 em 30 passos). Um estado aplicado no passo N
aparece no buffer bruto no fim de N e no `PDS_PERIPHERAL` em N+1. A resolução dessa medida é de um
passo e não prova equivalência dentro do VBlank (revisão do Codex). Os roteiros "indice passo" já
embutem essa latência, e o provedor nativo tem de reproduzir o ponto de amostragem (decisão 2).

### 2.2 Protocolo Maple efetivamente usado (16 290 VBlanks até a área 1)

| Porta | Comando | Contagem | Observação |
|---|---|---|---|
| A | DEVICE_INFO (0x01) | 1 | na inicialização: `support` e `info` vêm daqui |
| A | GET_CONDITION (0x09), função controle | 16 289 | um por VBlank |
| B, C, D | DEVICE_INFO | 16 290 cada | sondagem de conexão a cada VBlank (portas vazias) |

Nenhum comando de VMU, LCD ou vibração na rota: não há subunidades. Buffers de recepção:
`8C27E1A0` e `8C2841A0` (buffer duplo, 0x1800 por porta).

### 2.3 Dependências de hardware Maple (sonda do N0)

| Função (SDK) | Acesso | Vezes | Papel |
|---|---|---|---|
| `8C08BD74` | escreve MDSTAR, MDTSEL, MDEN, MDST, 6C80, 6C8C, 6CE8 | 1 | inicialização do sistema (`syHw`) |
| `8C09727C` | escreve MDTSEL, MDEN, 6C80, 6C8C; lê a ROM | 1 | inicialização do Maple (via `8C08F0E0` ← `8C15C10C`) |
| `8C0975C0` | escreve MDSTAR, MDST; lê MDST | 7 982 | disparo do DMA no VBlank |
| `8C0976DE` | lê MDST | 7 982 | espera ou checagem do fim do DMA |
| `8C098EC2` | constante `A05F6C00` | — | base dos registradores |

Fronteiras "controles" do N0 reclassificadas:
- `8C08B9E0`, `8C08BA06` e `8C08BA30` são *wrappers* do jogo para configuração de vídeo/VBlank
  (`8C08CE3A`, `8C08CECA`). Chegam ao Maple só pela cadeia do VBlank e não são API de entrada.
- `8C15E6D8` é o `pdLcd` (VMU, seção 2.5).
- As fronteiras reais do controle são as do `pd` (seção 2.4).

### 2.4 SDK `pd`: funções e a estrutura `PDS_PERIPHERAL`

| Função | Papel | Evidências |
|---|---|---|
| `8C15C10C` | inicialização do `pd` (modo em `8C2D8EB0` = 0, inicializa a camada Maple) | literal `8C2D8EB0`; chama `8C08F0E0` → `8C09727C`; chamada pelo boot `8C08BB40` |
| `8C15C4E6` | servidor `pd` (24 unidades, cálculo de `old`, `off`, `press` e `release`) | laço de 24 × 0x34 sobre `8C2D8970`; chamado por `8C173044` a cada passo; RAM medida |
| `8C15C24C` | decodificador do controle (condição bruta → campos) | lê o buffer bruto; RAM medida (UP, analógico, gatilhos) |
| `8C15C668` | `pdGetPeripheral(porta)` = `8C2D8970 + 0x34·porta` | código; chamada pelo jogo `8C064878` (`jsr @r8`) |
| `8C15C1A2` | limpa a unidade ausente | chamada pelo servidor no ramo "sem dispositivo" |
| `8C15C3D4`, `8C15C490`, `8C17B60C`, `8C08D108(6)` | ramos auxiliares (outro tipo de dispositivo, tabela `8C2D8E50`) | **a identificar no N3-0** |

`PDS_PERIPHERAL` (0x34 bytes por unidade; porta A = `8C2D8970`). O layout é o do
`PDS_PERIPHERAL` documentado do Shinobi, e cada campo foi medido:

| Desl. | Campo | Valor/semântica medida |
|---|---|---|
| 0x00 | id | `000F06FE` |
| 0x04 | support | `000F06FE` = dados da função Maple `FE060F00` com bytes invertidos; bits 16–21 habilitam r, l, x1, y1, x2, y2 |
| 0x08 | on | botões em lógica positiva (bits do controle Dreamcast); gatilho ≥ 128 acrescenta 0x10000 (byte 2 da condição, `r`) ou 0x20000 (byte 3, `l`) |
| 0x0C | off | `~on` |
| 0x10 | press | `(on ^ old) & on` (modo 0) |
| 0x14 | release | `(on ^ old) & off` |
| 0x18/0x1A | r, l | bytes 2 e 3 da condição (0–255) |
| 0x1C/0x1E | x1, y1 | byte − 128 (−128..127) |
| 0x20/0x22 | x2, y2 | idem (o controle padrão não tem: `support` sem bits 20–21) |
| 0x24 | name | `8C2CFFBC` ("Dreamcast Controller") |
| 0x2C | old | `on` **lido da própria estrutura** antes do decode, ou seja, já com as escritas do jogo (seção 2.6) |
| 0x30 | info | `8C2CFFA8`: +0 bit 0 = conectado, +4 = função, +0x14 = nome (vem do DEVICE_INFO) |

Mapeamento R/L: os bytes 2 e 3 seguem o layout do runtime (`maple.cpp`: R no byte 2, L no 3). A
conferência com o Flycast fica no N3-0. O teste LT+RT deu `on = 0x30000`, `r = l = 255`.

### 2.5 Outros usuários da camada Maple (ficam fora do N3)

| Biblioteca | Funções | Uso | Etapa |
|---|---|---|---|
| `bu` (backup/VMU) | `8C09945A`, `8C0994B4` (25–28 chamadores em `8C091xxx–8C092xxx`), `8C094FF0` | detecção e acesso à VMU | N4 |
| `pdLcd` | `8C15E5BC`, `8C15E6D8`, `8C15E73C` | tela da VMU (192 B = 48×32); chamada pela animação do jogo `8C01D104` | N4 |
| `pdVib` | banner presente | nenhuma chamada identificada na rota | conferir no N3-0 |
| `kd` (1.23) | `8C17B60C`? | provável teclado Dreamcast, ramo do servidor para outro tipo de dispositivo | conferir no N3-0 |

Medido: com o Maple sem nenhum dispositivo, o boot mostra a mesma tela "VMU not found" (14
quadros iguais byte a byte até o VBlank 1 400). O caminho da VMU não depende do controle simulado
no boot. LOAD GAME sem dispositivos ainda não foi medido (exige entrada nativa: N3-2).

### 2.6 Jogo: o que pertence ao gameplay

| Função | Papel |
|---|---|
| `8C064878` (chamada pelo laço principal logo depois do servidor) | percorre a lista de portas `8C137A70` (só a porta A, depois −1) e aceita a primeira com `support` contendo `000D040E` ou `000C060E`. Guarda o PDS* em `8C24CCB0`. Sem controle compatível, **escreve na estrutura do SDK**: `on = press = release = r = l = x1 = y1 = 0`, `off = −1`. Direcional → analógico: UP/DOWN → y1 = ∓126, LEFT/RIGHT → x1 = ∓126. Zona morta \|x1\|, \|y1\| < 20 → 0. Soft reset: A+B+X+Y em `on` e START em `press` → bit 2 em `[8C24D2CC+0x25AC]` |
| `8C03AC2E` (via `8C03BAE0`) | palavra de ações em `8C24C5D0`: `on` mais analógico → direcional com limiar ±48; 11 ações (máscaras em `8C0ABF08`: ↑ ↓ ← → A B X Y 0x20000 0x10000 START) com auto-repetição (`8C0C6A6D..6F` = 8, 4, 8) |
| `8C04E440` | consome bits de `press` (escreve `press` no SDK) |
| `8C08B544` | player de FMV: START em `press` pula o vídeo |
| 24 outras funções do jogo | leem `on`, `off`, `press`, `x1`, `y1` (menus, inventário, câmera, movimento) |

Campos do contrato que o jogo usa: `on`, `off`, `press`, `x1`, `y1`, `info+0` (leitura) e a
reescrita de `on/off/press/release/r/l/x1/y1`. O jogo não lê `r`, `l`, `x2`, `y2`, `old`,
`name` nem `extend`. Os gatilhos chegam ao jogo só como bits digitais.

### 2.7 Fronteira protocolo × gameplay

| Pertence ao protocolo Dreamcast (sai do produto) | Pertence ao gameplay (fica, depois é elevado no N9) |
|---|---|
| registradores, tabela e fim de DMA, MDST, ISTNRM do Maple | lista de portas e máscaras de compatibilidade (`8C064878`) |
| quadros DEVICE_INFO/GET_CONDITION, condição ativa em 0, sondagem de conexão | pós-processamento: direcional → analógico, zona morta 20, soft reset |
| buffers de recepção, camada `per` (`8C08EFF8`, `8C08F332`, `8C0975C0`, `8C08F4B6/CC`) | palavra de ações, limiar 48, auto-repetição (`8C03AC2E`) |
| decodificador e servidor `pd` (`8C15C24C`, `8C15C4E6`) | todos os consumidores de `on/off/press/x1/y1` |
| `InputSource::read(porta, seq)` indexado por GET_CONDITION | mapa lógico de botões e ações |

O contrato entre os dois lados é o `PDS_PERIPHERAL` (2.4). O provedor nativo implementa esse
contrato a partir de entradas nativas, sem passar pelo protocolo.

## 3. Arquitetura-alvo

```
native/platform/input/           (produto; C++20, sem nada do Dreamcast)
  InputSystem       dispositivos, conexão/desconexão, amostra por quadro (latch), atribuição a jogadores
  backends          SDL3 Gamepad (Xbox, PlayStation, Switch, genéricos pelo banco do SDL),
                    teclado (eventos da janela), roteiro (testes), XInput como reserva
  ActionMap         remapeamento por ação lógica (arquivo do usuário), zona morta, curvas,
                    limiar digital dos gatilhos, combinações (soft reset)
  ControllerState   botões lógicos (enum próprio), eixos float, gatilhos float, conectado
transitional/pad_adapter          (some no N9, quando os consumidores do jogo forem elevados)
  slots em 8C15C4E6 (servidor) e 8C15C668 (getter). O init 8C15C10C NAO e substituido: a
  rotina original roda (inicializa per/Maple, que a VMU usa ate o N4) e o adaptador se inicializa
  depois dela
  escreve o PDS_PERIPHERAL na RAM guest a partir da última amostra do InputSystem,
  com a mesma semântica de 2.4 (old lido da estrutura, press/release, support)
  info/name: estrutura guest estável sintetizada pelo adaptador numa região própria (mapa de
  regiões do N0), com o conteúdo do DEVICE_INFO legado (conectado, função, área, nome...)
runtime/maple (legado)            só para a VMU até o N4 e para --controles legado
```

### 3.1 Decisões de projeto
1. **Fronteira = `pd`, não Maple.** Substituir o servidor e o getter tira o controle do protocolo
   inteiro, sem simular DMA, registradores nem respostas. Substituir a camada `per` (8C08F4B6/CC)
   manteria o formato bruto do Dreamcast: rejeitado. Elevar os 28 consumidores agora seria N9.
2. **Amostra por quadro (latch).** O `InputSystem` congela o estado no ponto lógico equivalente ao
   GET_CONDITION de hoje (2.1), não na entrega do VBlank-In.
   - Até o N2c, o marcador é temporário e só de tempo: a entrada do disparo `8C0975C0`, pelo
     tratador de VBlank do SDK. Nada do protocolo é usado.
   - O servidor do passo seguinte consome a última amostra: mesma latência do hardware e dos
     roteiros "indice passo".
   - Depois do N2c, o latch passa para a fronteira de quadro nativa.
3. **Fidelidade analógica, regras exatas.**
   - Eixo do host em float [−1, 1] → byte = clamp(round_half_away(127,5·v + 127,5), 0, 255).
   - O campo do SDK é byte − 128 (−128..127): centro = 128 → 0. Extremos 0 → −128 e 255 → 127,
     como `extu.b` + `add #-128` em `8C15C300-324`.
   - Sem zona morta extra no modo fiel. O jogo aplica as suas, estritas:
     - zona morta `−20 < v < 20` → 0 (−20 e 20 permanecem);
     - direcional só com `v < −48` ou `v > 48` (−48 e 48 não geram direção).
   - Gatilho [0, 1] → byte pela mesma regra; o bit digital só com byte ≥ 128 (bit 7).
   - Zona morta do host configurável só para *drift* de hardware, desligada no modo fiel.
4. **Porta única.** O jogo só lê a porta A. O "jogador 1" do `InputSystem` alimenta a porta A. Com
   vários controles: o último com atividade assume (configurável) ou fica fixo pela atribuição do
   usuário.
5. **Desconexão/reconexão.** Desconectado → a unidade fica "sem dispositivo" (o que o servidor
   legado faz em `8C15C1A2`). O jogo trata isso em `8C064878` (estado zerado). Reconexão volta no
   quadro seguinte. Pausa automática ao desconectar é melhoria (M9), fora do modo fiel.
6. **Maple no modo nativo:** sem dispositivo de controle (como `BS_MAPLE_PORTS=none`). O DMA
   continua só para a sondagem da VMU até o N4 e é removido no N2c.
7. **Seleção em execução:** `--controles nativo|legado`, no padrão das opções do N1/N2. O padrão
   continua `legado` até os portões e a decisão do mantenedor.

## 4. Plano em subetapas

### N3-0: contrato e instrumentação (sem mudança de comportamento)
- Diagnóstico `BS_PAD_TRACE`: por passo de jogo, os campos do PDS da porta A, `8C24C5D0..E0` e o
  bit de soft reset. Linha de base dos 4 roteiros "indice passo".
- Identificar os ramos pendentes de 2.4 e 2.5 (`8C15C3D4`, `8C15C490`, `8C17B60C`, `8C08D108`,
  `pdVib`, `kd`), cada um com duas evidências (seção 3 do plano-mestre).
- Conferir com o Flycast: R/L nos bytes 2/3, `id`, `info` e o comportamento ao desconectar no meio
  do jogo.
- Oráculo de conexão no legado: presença roteirizada por passo no Maple (hoje
  `BS_MAPLE_PORTS` é lido uma vez, em `static const`). Casos:
  - desconectar com botão apertado;
  - primeiro passo ausente;
  - reconectar neutro;
  - reconectar com botão já apertado.
- Medir LOAD GAME e OPTIONS sem dispositivos no Maple e registrar o conteúdo apontado por
  `info`/`name` em cada caso.
- Converter `newgame_area1.txt`, `newgame_exploracao*.txt` para "indice passo", com prova de
  quadros iguais. Esses roteiros hoje são indexados pela contagem de GET_CONDITION, que deixa de
  existir no modo nativo.
- Teste de contrato: função C++ pura `decode(condição, support, old, modo) → campos` comparada com o
  decodificador legado (`8C15C24C` + servidor), ctest com a imagem. Matriz demonstrável, não
  "100%":
  - os 256 valores de cada byte (gatilhos e quatro eixos), um de cada vez;
  - as 64 combinações dos 6 bits de `support`;
  - cada bit de botão sozinho e em pares;
  - combinações simultâneas de gatilhos com botões;
  - padrões de `old`/`on` (0, tudo, alternados, igual, complemento);
  - os dois modos do servidor (`8C2D8EB0` = 0 e ≠ 0);
  - primeiro quadro e unidade ausente;
  - pontos de fronteira −49, −48, −47, −21, −20, −19, 0, 19, 20, 21, 47, 48, 49 e gatilhos 127/128.
- **Aceitação:**
  - traço determinístico (duas execuções iguais);
  - contrato igual ao legado em toda a matriz;
  - roteiros convertidos com quadros idênticos;
  - achados e artefatos registrados.

### N3-1: núcleo nativo de entrada (host, sem tocar no jogo)
- `native/platform/input`: `ControllerState`, botões lógicos, `InputSystem` com latch, `ActionMap`
  (remapeamento em arquivo TOML do usuário; o toml++ já é dependência), backends SDL3 Gamepad e
  teclado, backend de roteiro por passo, eventos de conexão.
- O caminho legado passa a ler do mesmo latch (o Maple só traduz).
- **Aceitação:** unitários (mapeamento, remapeamento, zona morta, conversão de eixos e gatilhos,
  conexão); com `--controles legado`, todas as suítes e o `test_assistido` byte a byte iguais a
  `3a3fc92`; teclado e controle funcionando na Preview (manual).

### N3-2: provedor nativo `pd` (`--controles nativo`, opcional)
- `transitional/pad_adapter` com slots em `8C15C4E6` e `8C15C668`. O `8C15C10C` original continua
  rodando, e o teste confirma que `8C08F0E0` (init `per`) executa no modo nativo.
- O adaptador escreve as 24 unidades (porta A conectada ou não; demais ausentes) com a semântica
  de 2.4, inclusive `old` lido da estrutura (o jogo a reescreve em `8C064878`).
- `info`/`name` apontam para a estrutura sintetizada (seção 3), e os testes comparam o conteúdo
  apontado, não só o ponteiro.
- Maple sem dispositivo de controle no modo nativo.
- **Prova de fechamento:**
  - dinâmica: no modo nativo, 0 GET_CONDITION, 0 execuções de `8C15C24C` e `8C15C4E6` (rastreio de
    chamadas);
  - estática: todas as chamadas a `8C15C4E6` e `8C15C668` passam por slot, inclusive a indireta de
    `8C064878`.
- **Aceitação:**
  - `BS_PAD_TRACE`, quadros, leituras, áudio e estado por passo iguais entre `legado` e `nativo` nos
    4 roteiros "indice passo" e nos convertidos;
  - negativos detectados;
  - "VMU not found" e LOAD GAME inalterados.

### N3-3: recursos do produto
- Remapeamento completo (arquivo e documentação), perfis por GUID de dispositivo, vários controles
  (troca a quente), desconexão/reconexão, teclado com mapa padrão revisado, mouse só como botões (se
  o mantenedor quiser).
- Vibração: interface pronta (`IInput::vibrate`), ligada só se o N3-0 achar uso de `pdVib`.
- **Aceitação:** roteiros de desconexão/reconexão (porta some e volta em passos fixos) com o mesmo
  resultado do legado; o remapeamento produz o mesmo efeito no jogo que o roteiro com os botões
  equivalentes; Xbox e teclado testados manualmente na Preview.

### N3-4: proposta de padrão
- Regressão completa sequencial, desempenho (tolerância B9) e revisão do Codex.
- O mantenedor decide trocar o padrão para `nativo`. `legado` fica por um ciclo como rollback.

## 5. Testes de equivalência previstos (`tools/dist/test_controles.ps1`)

| Caso | Critério |
|---|---|
| C1 | `BS_PAD_TRACE` igual (legado × nativo) nos 4 roteiros "indice passo" e nos convertidos |
| C2 | quadros no fim de cada passo, leituras de arquivo, escritas na AICA e estado lógico a cada 10 passos iguais (reuso do `compara_modos.py`) |
| C3 | Maple no modo nativo: 0 GET_CONDITION; `8C15C24C` e `8C15C4E6` não executam |
| C4 | negativos, todos acusados: A↔B trocados; latch adiantado 1 passo; latch dentro do mesmo VBlank, mas na entrega do VBlank-In em vez do disparo (com transição injetada entre os dois); limiar do gatilho em 127; `old` vindo do estado nativo em vez da RAM; `info` sem o bit de conectado |
| C5 | desconexão/reconexão roteirizada (os 4 casos do N3-0): mesmo traço do oráculo legado com presença roteirizada |
| C6 | remapeamento: arquivo com A↔B mais roteiro com B produz o traço do roteiro original com A |
| C7 | soft reset A+B+X+Y+START: mesmo efeito nos dois modos |
| C8 | determinismo (duas execuções), `--tempo nativo` e `experimental`, build com mods |
| C9 | portões permanentes: `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_assistido` |

## 6. Riscos

| Risco | Mitigação |
|---|---|
| Ponto de amostragem diferente (dentro do VBlank ou de 1 passo) | latch no ponto do disparo (decisão 2); C1 e negativos C4 |
| Init `per`/Maple pulado (VMU quebrada até o N4) | `8C15C10C` não é substituído; teste de execução de `8C08F0E0` |
| `info`/`name` sem produtor no modo nativo | estrutura sintetizada estável; conteúdo comparado |
| O jogo escreve na estrutura do SDK (`8C064878`, `8C04E440`) | o adaptador lê `old` da RAM e reescreve tudo a cada servidor, como o legado; negativo C4 |
| Maple sem controle muda o tempo do DMA (bits × 100 ciclos) no modo estável | medir no N3-2. Se quadros divergirem só por isso, a reserva é o Maple responder um controle neutro, não consumido, até o N2c |
| Ramos do servidor não identificados (`kd`, `8C08D108`) | N3-0 com duas evidências; porta A só com controle padrão |
| Fidelidade analógica (curvas do host) | modo fiel linear; zona morta do host opcional e documentada |
| Roteiros antigos indexados por leitura | conversão no N3-0 |
| `pdVib`/`kd` usados em caminho não coberto | varredura estática no N3-0; matriz de cobertura |
| Foco da janela | `releaseAll` ao perder o foco (já existe), mantido no `InputSystem` |

**Rollback:**
- `--controles legado` (padrão até o N3-4);
- backup `dist/BlueStinger-Preview.<etapa>-<commit>` e `analysis/rollback-antes-n3/` (executáveis
  e gerado com hashes), como no N2b-2.

## 7. Decisões do mantenedor

1. **Backend de controles:** SDL3 Gamepad (recomendado: o SDL3 já é dependência e cobre Xbox,
   PlayStation, Switch e genéricos) com XInput como reserva, ou só XInput.
2. **Formato e local do remapeamento:** TOML na pasta do usuário (recomendado).
3. **Maple no modo nativo:** sem dispositivo (recomendado) ou controle neutro até o N2c.
4. **Mouse:** fora do N3 (recomendado) ou só como botões.
5. **Ordem:** começar pelo N3-0 (sem mudança de comportamento).

## 8. Revisão do Codex (`reviews/codex-20261017-145645.md`)

Todos os achados foram aceitos. O ALTA foi conferido no binário: `8C08BB9C-BBA4` chama
`8C15C10C` com `r5 = 8C27E190` e `r6 = 8C28A1B0`, e `8C15C180-198` chama `8C08F0E0` quando os dois
não são nulos.

| Achado | Onde entrou |
|---|---|
| [ALTA] Substituir `8C15C10C` pularia o init `per`/Maple que a VMU usa | Seção 3 e N3-2: o init original continua; teste de execução de `8C08F0E0`; risco na seção 6 |
| [MÉDIA] O VBlank-In não é o ponto de amostragem atual (é o MDST dentro de `8C08EFF8` → `8C0975C0`) | Seção 2.1 e decisão 2: latch no ponto do disparo; negativo dentro do VBlank em C4 |
| [MÉDIA] `BS_MAPLE_PORTS` é lido uma vez; não serve de oráculo de desconexão | N3-0: presença roteirizada por passo no legado, 4 casos; C5 |
| [MÉDIA] Quem cria o objeto de `info`/`name` no modo nativo | Seção 3 e N3-2: estrutura sintetizada estável, conteúdo comparado; negativo em C4 |
| [MÉDIA] "100% das entradas" não demonstrado | N3-0: matriz explícita (bytes, 64 combinações de `support`, `old`, modos, combinações) |
| [BAIXA] Limiares e quantização subespecificados | Decisão 3: regras exatas e pontos de fronteira |
| [BAIXA] Medições sem artefatos | Seção 1: `analysis/n3/` com hashes e comandos; linha de base por ferramenta no N3-0 |

O Codex não verificou as medições dinâmicas (artefatos ainda não referenciados na hora da revisão),
a conferência com o Flycast, `pdVib`/`kd`, LOAD GAME sem dispositivos nem a contagem exata de
consumidores. Todos esses pontos estão no N3-0.
