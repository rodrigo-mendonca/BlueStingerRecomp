# N3-0: contrato, instrumentação e testes de referência dos controles

Data: 2026-10-17. Autorização do mantenedor: **N3-0 apenas** (plano em `docs/N3-PLANO.md`,
aprovado com as decisões: SDL3 Gamepad com XInput de reserva; remapeamento em TOML na pasta de
configuração do usuário; no caminho nativo, sem controle virtual Maple e sem DMA de controle, com o
Maple preservado só para a VMU até o N4; mouse fora do N3). Base: `3a3fc92`. Nada do N3-1/N3-2 foi
implementado, e o comportamento padrão não mudou: tudo o que entrou é diagnóstico ligado por
variável de ambiente.

## 1. O que entrou

| Peça | Onde | Papel |
|---|---|---|
| Contrato do servidor `pd` | `transitional/pad_contract.{h,cpp}` | Função pura (sem `Sh4Context`) do que o servidor 8C15C4E6 escreve em cada uma das 24 unidades `PDS_PERIPHERAL`: classificação (controle, teclado, ausente, subunidade), código de estado (8C15C490), decodificador (8C15C24C), "sem dispositivo" (8C15C1A2), bordas `press`/`release` nos dois modos, info sintetizada do controle padrão (0x78 bytes). É a referência do provedor nativo (N3-2). |
| Sonda de contrato | `transitional/pad_probe.{h,cpp}`, `BS_PAD_CONTRATO=<arq>` | Envolve o servidor legado (e 8C08F4B6, para ver a condição bruta de cada unidade), confere cada passada com o contrato e grava contagens, cobertura e divergências. Exige o build com mods (slots). Mutações `BS_PAD_CONTRATO_MUTACAO` (negativos) e `BS_PAD_MODO` (modo ≠ 0). |
| Registro por passo | `app/game_host.cpp`, `BS_PAD_TRACE=<arq>` | A cada passo de jogo: PDS da porta A0 depois do pós-processamento do jogo, ponteiro do jogo (8C24CCB0), info, palavra de ações (8C24C5D0..E0), pedido de soft reset (bit 2 de 8C24F878). |
| Registro de leituras | `runtime/maple`, `BS_PAD_LEITURAS=<arq>` | Cada GET_CONDITION: leitura, passo, VBlank, estado servido. |
| Presença roteirizada | `runtime/input`, `runtime/maple` | Estado `DESCONECTADO` (porta A vazia para todos os comandos; o SDK volta a sondar com DEVICE_INFO). Só em roteiros por passo ou por VBlank: com índice por leitura a porta vazia não gera leitura. |
| Valores exatos | `runtime/input` | `AX=n`, `AY=n`, `A2X=n`, `A2Y=n`, `LT=n`, `RT=n` (0..255) nos roteiros e no gameplay assistido; o texto sai exato (os extremos mantêm os nomes antigos). O segundo analógico passa a vir da fonte de entrada (antes, do estado fixo do Maple; o padrão é o mesmo, 0x80). |
| Transição injetada | `runtime/input`, `runtime/maple` | `indice transicao`: o estado muda no primeiro acesso ao MDST depois de cada VBlank-In, dentro do tratador do SDK (8C08EFF8 → 8C08F332) e antes do disparo do DMA (8C0975C0). É o ponto entre as duas amostragens possíveis. |
| Mutações de traço | `BS_PAD_MUTACAO` | `troca_ab` (A/B trocados no Maple), `passo_adiantado` (amostra um passo antes), `amostra_vblank_in` (amostra na entrega do VBlank-In em vez do disparo do DMA). |
| `BS_MAPLE_FUNCAO=<hex>` | `runtime/maple` | Dados da função no DEVICE_INFO (cobertura das 64 combinações de `support`). |
| Ferramentas | `tools/dev/pad_roteiros.py`, `pad_traco.py`, `converte_roteiro.py`, `pds_consumidores.py` | Geração dos roteiros de cobertura; comparação e conferências do traço; conversão de roteiros por leitura para "indice passo"; varredura dos consumidores. |
| Roteiros convertidos | `tools/dist/roteiros/passo_newgame_area1.txt`, `passo_newgame_exploracao.txt` | Os roteiros por leitura de `test_gameplay`/`test_n2b*` em "indice passo". Os originais ficam. |
| Suíte | `tools/dist/test_controles.ps1` (K1–K13) | Seção 4. |
| Unitários | `tests/unit/test_pad_contract.cpp` (5), `test_input.cpp` (+3) | 221 no total. |

## 2. Contrato do servidor `pd` (fechado)

Ordem por unidade *i* (0..23; porta = *i*/6, unidade = *i*%6), conferida no disassembly:
1. `info = per_info(i)` (8C08F4CC); `old = on` **lido da RAM** (já com as escritas do jogo);
   `estado[i]` (8C2D8E50) = 0 para resposta 0/5/6/8/0xFF, `FF0000FC` para 0xFC, `FF000000` senão.
2. Subunidade (*i*%6 ≠ 0; 8C08D108 é o resto da divisão do compilador): só `name = info+20`.
3. Unidade principal:
   - controle (`*info & 0x21`, erro 0, resposta 8): `support = id` = bytes de `info+4` invertidos,
     `name = info+20`, e o decodificador;
   - teclado (`*info & 0x40`): 8C17B60C, fora do contrato (0 ocorrências em todas as rotas);
   - senão, sem dispositivo: `id = support = 0`, eixos e gatilhos 0, `extend = 0`,
     `name = "(no device)"` (8C1DACAE).
4. Bordas: `off = ~on`; modo 0: `press = (on^old) & on`, `release = (on^old) & off`; modo ≠ 0:
   `press = ~(on^old) | on`, `release = ~(on^old) | off`. Na desconexão, `release` = o `on`
   anterior (as bordas sobrescrevem a limpeza).

Decodificador: `on` = botões ativos em 0 invertidos (modo 0), gatilho com bit 7 (byte ≥ 128) vira
0x10000 (R, byte 2) ou 0x20000 (L, byte 3); `r`, `l`, `x1`, `y1`, `x2`, `y2` = byte (eixos − 128),
cada um só se o bit 16..21 de `support` estiver ligado.

Info do controle padrão (medida na RAM, 0x78 bytes): flags 1; função `00 0F 06 FE`; `01 FF`;
nome com 30 bytes e terminador; licença com 60 bytes e terminador; consumo 0x01AE/0x01F4.

## 3. Consumidores do `PDS_PERIPHERAL`

`tools/dev/pds_consumidores.py` (varredura de todo o binário: literais da variável 8C24CCB0, da
tabela 8C2D8970..8C2D8EB3 e do getter 8C15C668; rastreio de registradores; escapes). Duas passadas:
conservadora (um ponto de junção desfaz o rastreio) e otimista (`--sem-juncao`). Resultados em
`analysis/n3/consumidores.tsv` e `consumidores_otimista.tsv`.

**Inventário conservador do jogo: 28 funções**, todas obtendo o ponteiro pela variável 8C24CCB0.
Não é prova de fechamento: a análise é linear e não vê ponteiros derivados por aritmética de outra
base.

Revisão manual das marcas:
- **Escapes de 8C064878:** `mov.l r0,@r14` é a gravação do ponteiro na própria variável. O
  "gravado em +0" em 8C0648E6 é dado do pool lido como instrução.
- **Escape de 8C17B60C (SDK, teclado):** o retorno do getter vai para a pilha local.
- **Passada otimista:** acrescenta `y1` em 8C03AC2E e `x1` em 8C07AB40 (já vistos no disassembly)
  e um "argumento r5" em 8C0461D4. Esse último é falso positivo: o chamado 8C064468 (som posicional,
  id em r4 e posição em fr4–fr6) sobrescreve r5 antes de usá-lo.

| Uso | Funções |
|---|---|
| escolha da porta e pós-processamento (lê `support`, `on`, `press`, `x1`, `y1`; escreve `on/off/press/release/r/l/x1/y1`) | 8C064878 |
| palavra de ações com auto-repetição (`on`, `x1`, `y1`, `info`) | 8C03AC2E |
| consome bordas (lê e escreve `press`) | 8C04E440 |
| `press` (menus, confirmações, FMV) | 8C0405A0, 8C0407C0, 8C041328, 8C0414EE, 8C041F00, 8C042280, 8C0461D4, 8C04AAA6, 8C04B22C, 8C04B500, 8C063F4C, 8C078FC0, 8C07BC2C, 8C08B544 (player de FMV: START pula) |
| `on` e `press` | 8C045452, 8C052240 |
| `on` | 8C041ACE |
| `off` | 8C040F20, 8C04D800 |
| eixos `x1`/`y1` (câmera, movimento) | 8C03E340, 8C03F520, 8C041800, 8C04196E, 8C058720 (também `on`), 8C07AB40 (também `on`) |

O 8C08B544 aparece também com `release`, mas é falso positivo: chamada por vtable (`@(20,r2)` de
outro objeto).

**SDK:** o `pd` (8C15C10C, 8C15C1A2, 8C15C24C, 8C15C4E6, 8C15C686) e 8C17B60C (teclado, chama o
getter).

**Campos não usados pelo jogo:** `r`, `l`, `x2`, `y2`, `old`, `name` e `extend`.
- Evidência estática: nenhuma das 28 funções os lê.
- Evidência comportamental (K11): com lixo nesses campos depois de cada passada do servidor,
  quadros, traço e estado do jogo a cada passo ficam idênticos nas 4 rotas assistidas, cerca de 40
  mil passadas.
- A conclusão vale **para essas rotas**: salas não visitadas, chefes e o resto da campanha não foram
  exercitados. O provedor nativo escreve esses campos com a semântica do contrato, então a
  afirmação só reduz risco e não é premissa do N3-2.

## 4. Testes (`tools/dist/test_controles.ps1`)

Ver a seção 6 para o resultado.

| Caso | O que prova |
|---|---|
| K1 | Contrato = legado em todas as passadas da matriz; cobertura: 256 valores de R, L, x1, y1; 12 botões; 66 pares; gatilho com botão; bordas. K1b: 256 valores de x2 e y2 (`support` com os bits 20–21) e os 4 padrões de `old` × `on` |
| K2 | 64 combinações dos bits 16..21 do `support`: contrato = legado, e o SDK viu as 64 |
| K3 | Modo ≠ 0: contrato = legado (a sonda devolve ao jogo a tabela do modo 0 para ele seguir) |
| K4 | Negativos do contrato acusados: A↔B, gatilho em 127, `old` do estado nativo, info sem conectado |
| K5 | Determinismo (duas execuções do release; duas do build com mods) e paridade release × mods |
| K6 | Limiares do jogo nos 30 pontos de fronteira; contrato no gameplay |
| K7 | Desconexão com A, reconexão neutra, reconexão com A (borda `press` no 1º passo), sem nova borda depois |
| K8 | Porta vazia desde o boot, conexão, START |
| K9 | Soft reset A+B+X+Y+START (referência legada); controle sem START |
| K10 | Negativos do traço: A↔B no Maple, amostra adiantada um passo, amostra no VBlank-In com a transição injetada depois dela e antes do disparo; controle (roteiro por passo, sem diferença) |
| K11 | Campos não usados (lixo) nas 4 rotas assistidas: quadros, traço e estado do jogo a cada passo iguais; contrato nas rotas |
| K12 | Roteiros convertidos = originais (traço com tempo e quadros por VBlank) |
| K13 | Padrão inalterado: quadros e leituras = executável anterior ao N3-0 (obrigatório) em `passo_cgate`, `passo_heliporto` e `newgame_area1`; o traço não muda os quadros |

## 5. Achados

1. **O jogo escreve na estrutura do SDK** (8C064878 e 8C04E440), e o `old` do servidor vem da RAM:
   o provedor nativo tem de ler `old` da RAM (o negativo `old_nativo` diverge quando o jogo zera o
   `on`).
2. **Ponto de amostragem:** com roteiros por passo, amostrar no VBlank-In ou no disparo do DMA dá
   o mesmo resultado. Só uma mudança de entrada entre os dois pontos os separa (K10, com a transição
   no primeiro acesso ao MDST). No N3-2, o latch fica no ponto do disparo e esse negativo continua
   no portão.
3. **A tela de título consome a borda** `press` do START (o jogo escreve `press`): conferências de
   botão usam `on`.
4. **Soft reset (legado):** A+B+X+Y+START faz o jogo chamar o menu do BIOS (syscall em 8C001008,
   valor 0x200), que o HLE não implementa: a execução termina com erro. É anterior ao N3; o K9 é a
   referência. Decisão do produto (sair do jogo ou reiniciar) fica para quando o BIOS sair (N8).
5. **Roteiros por leitura:** o estado de cada passo é o da última leitura do passo, e a conversão é
   exata (K12). A variante `newgame_exploracao_n2b2.txt` era só compensação do índice por leitura
   no experimental e não precisa de versão por passo.
6. **Desconexão:** o jogo não mostra aviso (quadros conferidos durante a porta vazia,
   `analysis/n3/n3-0/con_mosaico.png`). Com a porta vazia, 8C064878 não acha controle compatível e
   zera a entrada. Reconectar volta 2 a 3 passos depois (sondagem DEVICE_INFO do SDK).
7. **Inventário de consumidores:** é conservador, não prova de fechamento (seção 3). A prova dos
   campos não usados vale para as rotas exercitadas.

## 6. Resultados

Regressão completa de 2026-10-17 com `tools/dev/regressao.ps1`, retomada duas vezes sem refazer as
etapas aprovadas: uma reinicialização da sessão e o limite de 2 h do ambiente. Fonte conferida
idêntica.

**Executáveis** (SHA-256, 16 primeiros dígitos):
- release `B38C733BCBCE2DD8`;
- build com mods `9681BC40403723F9`;
- referência do K13: `analysis/rollback-antes-n3/exe/bluestinger-release.exe`, de `3a3fc92`,
  `4416C24261536C6F`.

| Etapa | Resultado |
|---|---|
| Builds release, debug, mods, investigação | ok |
| Unitários | 221 ok |
| ctest sem e com o disco | 100% |
| `make_builder`, `make_preview` | ok |
| `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_arquivos`, `test_ordem`, `test_tempo` | verdes |
| `test_n2b`, `test_cri`, `test_efeito`, `test_n2b2` | verdes |
| `test_assistido` | 28/0 |
| `test_controles` | **39/0** |

Saída completa da `test_controles` e estado da regressão: `analysis/n3/n3-0/test_controles_final.txt`
e `regressao_estado.tsv`.

**Destaques da `test_controles`:**
- K1: 2 585 passadas, 0 divergências; 256/256/256/256 valores; 12 botões; 66 pares; 342 bordas;
- K1b: x2 e y2 256/256; padrões de `old` 1/826/5/1;
- K2: 64 de 64 combinações de `support`; K3: modo 1, 333 passadas;
- K4: os 4 negativos acusados;
- K5: determinismo dos dois builds e paridade (9 558 passos);
- K6: 30 pontos de fronteira;
- K7: conexão e reconexão;
- K10: negativos com 84, 1 018 e 1 001 passos diferentes; controle com 0;
- K11: as 4 rotas, cerca de 40 mil passadas;
- K12: `area1` (3 470 passos) e `exploracao` (7 820) iguais aos originais;
- K13: 3 rotas iguais à referência.

## 7. Revisão do Codex

Duas rodadas (`reviews/codex-20261017-164009.md` e `-173236.md`), todos os achados aceitos e
corrigidos (registro no `STATUS.md`). O Codex conferiu no binário o contrato (estado, classificação,
`old` da RAM, gatilho no bit 7, eixos, bordas, sem dispositivo) e a ordem VBlank-In → MDST → disparo
que legitima o negativo do ponto de amostragem.

## 8. Riscos restantes e recomendação

**Riscos restantes:**
- prova dos campos não usados só nas rotas exercitadas;
- inventário estático conservador;
- `pdVib`/`kd` não exercitados (teclado: 0 ocorrências);
- conferência com o Flycast (bytes R/L, DEVICE_INFO, desconexão física) ainda não feita;
- soft reset legado termina em erro (syscall do BIOS sem HLE);
- o tempo do DMA do Maple sem controle no modo nativo só será medido no N3-2.

**Recomendação:** autorizar o **N3-1** (núcleo nativo de entrada no host: `ControllerState`, latch,
`ActionMap` com TOML, SDL3 Gamepad e teclado, roteiro por passo). O portão do N3-1 é o legado lendo
do mesmo núcleo com a `test_controles`, a `test_assistido` e o K13 inalterados.
