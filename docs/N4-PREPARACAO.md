# N4 (saves nativos): preparação

Data: 2026-10-18. **Só planejamento**, adiantado a pedido do mantenedor. O N4 não foi autorizado e
nada foi implementado. Fonte: inventário somente leitura do Codex sobre o binário
(`reviews/codex-20261018-220833.md`, privado). As identificações abaixo são hipóteses de trabalho até
a investigação do N4 conferir cada uma no disassembly. Itens sem evidência no binário estão marcados
"não verificado".

## Situação atual

O runtime não tem VMU: o jogo mostra "VMU not found", o LOAD GAME sem VMU funciona e não há como
salvar. O N4 entrega uma capacidade nova (salvar e carregar) por um provedor nativo. Não há caminho
legado funcionando que precise ser substituído.

## Fronteira proposta: API pública `bu`, não as rotinas internas

As funções `8C09945A`, `8C0994B4` e `8C094FF0` (citadas no `docs/N3-PLANO.md`) são predicados
internos da `bu` que consultam a camada `per`. Usá-las manteria o gerenciador, a FAT e o estado da VMU
originais, o que equivale a emular a VMU. A fronteira proposta são os pontos públicos:

| Endereço | Papel provável | Observado |
|---|---|---|
| `8C08BDEC` | `buInit(capacidade, máscara, work, callback)` | chamado por `8C01C41E` (capacidade 0x7A, máscara 3) |
| `8C08C074` / `8C08C07A` | registro dos callbacks de conclusão / progresso | `8C01C2D2` e `8C01C364` |
| `8C08CA34` | montagem da unidade (`buMountDisk`?) | nome não verificado |
| `8C08C1FC` | informação do disco | |
| `8C08C636` | existência de arquivo | `8C01C8CE` varre `BLUESTIN.000`–`.005` |
| `8C08C8D4` | `buLoadFileEx(drive, nome, dst, bloco_inicial, nblocos)` | string de erro no binário |
| `8C08C818` | gravação (`drive, nome, src, blocos`, pilha: tempo, flag) | ordem dos argumentos de pilha não verificada |
| `8C08C696` | estado da operação (−1 ocupada, 0 concluída) | polling com timeout de 0x1C2 iterações |
| `8C08C112` | último erro | |
| `8C08BEBC` | encerramento (`buExit`?) | não verificado |
| `8C08CC58`, `8C08CA5C` | tamanho e montagem da imagem VMS | rotinas puras: podem ficar no recompilado como oráculo |
| `8C15E5BC`, `8C15E6D8`, `8C15E73C` | `pdLcd` (tela da VMU, 192 bytes) | responder "LCD não suportado", desacoplado do save |

## Inventário das 13 fronteiras `bu` (2026-10-19)

Método: `analysis/hwscan/callgraph.tsv`, busca dos literais de 32 bits no `1ST_READ.BIN` e
`bs-disasm`, só leitura, durante a regressão do N3. "Fronteira" é a classificação atual em
`config/fronteiras_us.toml`. Os papéis continuam hipóteses até o trace do legado (incremento 1).

| # | Endereço | Papel (evidência) | Chamador no jogo | Fronteira hoje | Observação |
|---|---|---|---|---|---|
| 1 | `8C08BDEC` | init: registra o servidor periódico `8C08BDE8` (`bra 8C08BF50`) por `8C17BFB4` com prioridade `0x0A00` e chama o callback do 4º argumento (`8C08BE5A`) | `8C01C41E` (tail; vem de `8C0788A0`) | `cri`, `["tempo"]` | classificação errada; o servidor `8C08BF50` sonda as 8 unidades com `8C08C696` |
| 2 | `8C08C074` | registra o callback de conclusão (`8C01C2D2`) | `8C01C37A` | `cri`, `[]` | `8C01C37A` é referenciado só pelo literal `8C01C484` |
| 3 | `8C08C07A` | registra o callback de progresso (`8C01C364`) | `8C01C37A` (tail) | `cri`, `[]` | |
| 4 | `8C08CA34` | montagem `(drive, work, size)` | `8C01C740` (tail) | `cri`, `[]` | `work` e `size` vêm do callback (op `0x12`, abaixo) |
| 5 | `8C08C1FC` | informação do disco `(drive, dst)` | **só no callback** `8C01C2D2` (`8C01C31E`, `8C01C350`) | `cri`, `[]` | chamada reentrante, de dentro do callback de conclusão |
| 6 | `8C08C636` | existência de arquivo | `8C01C8CE` | `cri`, `[]` | varre `BLUESTIN.000`–`.005` |
| 7 | `8C08C8D4` | LOAD `(drive, nome, dst, bloco_inicial, nblocos)` | `8C01C4AE` (de `8C01CB18` e `8C01CE00`) | `cri`, `[]` | `8C01CB18` também faz polling (`8C01CC44`): segundo uso de LOAD, a identificar |
| 8 | `8C08C818` | SAVE `(drive, nome, src, blocos, time*, flag)` | `8C01C520` (de `8C01CCE0`) | `cri`, `[]` | |
| 9 | `8C08C696` | estado (−1 ocupada, 0 concluída) | `8C01C804`, `8C01CB18`, `8C01CCE0`, `8C01CE00` | `cri`, `[]` | também chamada pelo servidor `8C08BF96` (interna) |
| 10 | `8C08C112` | último erro | `8C01C804` | `cri`, `[]` | lida só depois de `C696 ≠ −1` |
| 11 | `8C08BEBC` | exit: falha (−1) se alguma das 8 unidades estiver ocupada; senão desregistra o servidor (`8C17C2D0`) | **nenhum direto**: só o literal `8C01C48C`, usado por `8C01C44A` (`jsr @r14` até `r0 = 0`) | **ausente** | `8C01C44A` não tem chamador nem literal: provável código morto. A ferramenta de fronteira só vê chamadas diretas |
| 12 | `8C08CC58` | tamanho da imagem VMS em blocos | `8C01C520` | `cri`, `[]` | pura |
| 13 | `8C08CA5C` | montagem da imagem VMS (cabeçalho, ícone, payload, zeros, CRC) | `8C01C520` | `cri`, `[]` | pura; única chamadora do CRC `8C08CDE2` |
| +1 | `8C08C7BE` | **papel não verificado** `(drive, r5, r6, r7)` (achado do Codex) | **nenhum direto**: só o literal `8C01C510`, usado por `8C01C4D2` (`jsr @r3`), que não tem chamador nem literal | **ausente** | mesmo caso do `8C08BEBC` |

Consequências para o fechamento:
- **Critério para literal morto** (o mesmo nos dois casos): toda entrada `bu` alcançável por literal no
  código do jogo entra no inventário e ganha slot, mesmo que a função que a usa não tenha chamador
  (`8C01C44A` e `8C01C4D2`). O slot só registra a chamada e devolve erro ("não suportado"). Se o
  fechamento contar uma chamada, a hipótese de código morto cai.
- A ferramenta de fronteira precisa enxergar as chamadas indiretas de `8C08BEBC` e `8C08C7BE`
  (literais no pool do jogo), ou o fechamento do N4 deixa caminhos `bu` fora da contagem.
- O servidor `8C08BF50` é **interno**. Com o init no slot, ele não é registrado. As conclusões passam a
  ser publicadas pelo adaptador, e o `BS_BU_FECHAMENTO` exige servidor `bu` = 0 passadas.
- Os pontos `8C15E534`, `8C15E5BC`, `8C15E6D8` e `8C15E73C` (`pdLcd`) ficam fora das 13 e são
  tratados à parte (respondem "LCD não suportado").

## Contrato dos saves assíncronos (proposta para o adaptador)

Leitura do disassembly dos receptores do jogo, a confirmar no trace (incremento 1).

**Estado do jogo por unidade:** bloco de 92 bytes em `8C1F3710 + drive·92` (o produto passa por
`extu.b`; a máscara 3 do init habilita só as unidades 0 e 1. O truncamento só causaria sobreposição a
partir da unidade 3, já que 3·92 = 0x114 vira 0x14).

**Callback de conclusão `8C01C2D2(drive, op, arg)`**, retorna 0:

| `op` | O que o jogo faz | Leitura |
|---|---|---|
| 1 | `arg = 0`: `+0 ← 1`, chama disk-info (`C1FC`) em `+20`; se ok, `+2 ← 1`; `+4 ← 0` | unidade pronta (montada) |
| 2 | libera o `work` de `+80` (`8C15C8D6`), limpa a unidade (`8C01C788`), `+76 ← 0` | unidade removida |
| `0x12` | `+76 ← 1`, `+84 ← arg·512 + 32`, `+88 ← arg` | cartão inserido: o jogo aloca `work` desse tamanho e chama a montagem (`CA34`) |
| outro | `+4 ← arg` (resultado), disk-info de novo em `+20` | fim de operação: `arg` é o código. SAVE pede a operação `0x0C` (`8C08C836`) e LOAD a `0x0E` (`8C08C8F2`). Que esses códigos chegam inalterados ao callback como `op` está **não verificado** |

Sempre `+16 ← 0` no fim, que zera o progresso.

**Callback de progresso `8C01C364(drive, a, b, c)`**: `+16 ← a`, `+8 ← b`, `+12 ← c`. O significado de
`a`, `b` e `c` (fase, blocos feitos, total?) é **não verificado**.

**Protocolo proposto, por unidade:**

| Estado | `C696` | `C112` | Entrada | Saída |
|---|---|---|---|---|
| `Idle` | 0 | último código | LOAD/SAVE aceito: devolve 0 | `Busy` |
| `Busy` | −1 | não lido pelo jogo | outro LOAD/SAVE na mesma unidade: devolve −1, sem alterar o pendente | `Busy` |
| `Busy` → `Complete` | 0 | código da operação | conclusão publicada pelo adaptador (ponto exato: trace) | callback de conclusão uma vez, com `op` e `arg` do legado |

- **Sucesso:** a escrita ou leitura já está durável e conferida *antes* de `C696` passar a 0. Para o
  SAVE, isso significa temporário relido e troca feita.
- **Erros:** `0xFF01` ausente, `0xFF03` não formatado, `0xFF05` não encontrado e `0xFF07` não abre
  (comprovados). Cheio, sem entradas, somente leitura e envelope inválido: códigos **não verificados**.
  Internamente ficam distintos, e o adaptador mapeia para o código que o trace mostrar.
- **Polling:** o jogo consulta `C696` até 0x1C2 (450) vezes por operação. O adaptador conclui num
  número determinístico de consultas ou de VBlanks, dentro desse limite, igual nos dois modos de tempo.
  A escolha vem do trace. Não pode depender do relógio de parede.
- **Abertos:** a ordem "estado 0 × callback × último erro", se a ocupação é global ou por unidade e a
  validade do último erro.

## Slots, formato VMS, CRC e preservação

- **Seis slots:** `BLUESTIN.000`–`.005`, enumerados por `8C01C8CE` com `exists`. Cada um ocupa 4 blocos
  (2 048 bytes).
- **Imagem VMS** (montada por `8C08CA5C`; offsets do formato VMS padrão, conferidos com o payload em
  `0x280`):

  | Offset | Tamanho | Campo |
  |---|---|---|
  | `0x000` | 16 | descrição curta ("BlueStinger") |
  | `0x010` | 32 | descrição longa ("BLUESTINGER SAVE DATA") |
  | `0x030` | 16 | identificador da aplicação |
  | `0x040` | 2 | número de ícones (1) |
  | `0x042` | 2 | velocidade da animação |
  | `0x044` | 2 | tipo de eyecatch |
  | `0x046` | 2 | CRC (gravado por `8C08CC36`) |
  | `0x048` | 4 | tamanho do payload (1 200) |
  | `0x060` | 32 | paleta do ícone |
  | `0x080` | 512 | ícone |
  | `0x280` | 1 200 | payload do jogo (`8C24F93C`) |
  | `0x730` | 208 | zeros até 2 048 (`8C08CC1C`: zera até o múltiplo de 512) |

- **CRC:** CRC-16 com polinômio `0x1021`, estado inicial 0, MSB primeiro. Pela leitura de `8C08CC28`,
  ele cobre os **1 840 bytes** sem o preenchimento (`r5 = r10`, o tamanho antes de arredondar), com o
  campo `0x46` ainda zerado nesse momento. A confirmar no unitário contra a rotina pura do recompilado.
- **LOAD:** a partir do bloco 1, `nblocos = 0`, destino `8C24F8BC`. Chegam 1 536 bytes, e o payload
  cai em `dest + 0x80` = `8C24F93C`. O bloco 0 (cabeçalho e CRC) não chega ao jogo.
- **Preservação:**
  - os 2 048 bytes ficam inalterados dentro do envelope, inclusive cabeçalho, ícone e zeros;
  - um LOAD nunca regrava;
  - um SAVE só substitui o arquivo depois da releitura do temporário, e o anterior vira `.bak`;
  - uma falha antes da troca deixa destino e `.bak` intactos;
  - envelope com magic, versão, tamanho ou checksum inválido é recusado sem apagar nada;
  - um nome fora de `BLUESTIN.00[0-5]` é recusado (não sai da pasta do cartão).

## Unitários que rodam sem o gameplay

Todos em `bs_unit_tests`, sem disco nem jogo; o "oráculo" são as rotinas puras recompiladas
(`8C08CA5C`, `8C08CC58`, `8C08CDE2`) chamadas num `Sh4Context` de teste:
1. payload de 1 200 bytes → imagem de 2 048, 4 blocos; o tamanho de `8C08CC58` é igual;
2. offsets da tabela acima e zeros finais; CRC nativo = CRC do oráculo, em vetores aleatórios fixos;
3. LOAD do bloco 1: 1 536 bytes, payload em `+0x80`;
4. envelope: ida e volta; recusa de magic, versão futura, tamanho ≠ 2 048, checksum e arquivo truncado;
5. armazenamento atômico com falhas injetadas (antes da escrita, depois da escrita, antes da troca e
   depois da troca): o save anterior sobrevive em todos os casos; `.bak` válido com destino inválido
   segue a política decidida;
6. nomes válidos e inválidos (`..`, separadores, `BLUESTIN.006`);
7. máquina do adaptador com um `ISaveStore` falso:
   - aceito → ocupado → concluído;
   - segunda operação durante ocupado recusada sem trocar a pendente;
   - callback uma vez só, com `op` e `arg`;
   - último erro;
   - conclusão em número fixo de consultas;
8. mapeamento ausente/não formatado/não encontrado/não abre → `FF01/03/05/07`; cheio, sem entradas e
   somente leitura distintos internamente;
9. coerência de `disk-info` × `exists` × armazenamento: cartão vazio, depois de criar um slot, depois
   de sobrescrever (sem consumir blocos) e depois de falhar (sem mudar nada). Os dois campos que o jogo
   copia (`8C01C93E`–`8C01C946`, offsets `+34` e `+38` do bloco de 92 bytes) têm significado **não
   verificado**;
10. recuperação em **nova abertura** do store, em cada estado intermediário de destino, temporário e
    `.bak` (antes da escrita, temporário completo, depois da troca, `.bak` sem destino): o resultado é
    sempre o save anterior ou o novo inteiro, nunca nenhum. Só depois disso o `C696` pode passar a 0.
    A política do `.bak` (automática ou com confirmação) tem de estar decidida antes deste teste;
11. **replay do oráculo**: a sequência de chamadas do `bu-trace` (c1, c2, c3) alimenta o adaptador com
    um `ISaveStore` em memória.
    - Iguais ao oráculo: os retornos de cada chamada, os `op` e `arg` dos callbacks e a posição deles
      na sequência, e o primeiro `C696 = 0`.
    - Livre: o número de `C696 = −1`, que só precisa ficar entre 1 e o limite do jogo.
    - O VMS gravado pelo adaptador passa no `n4_vmu.py valida` e é byte a byte igual ao VMS do Flycast
      para o mesmo payload.
    - Sem os arquivos do oráculo (`analysis/`, fora do git) o teste sai com SKIP.

## Menor sequência até save/load funcional

1. **Trace do legado** (sem mudar o jogo): `r4`–`r7` e PC de cada chamada `bu` e dos callbacks no boot
   e no menu de LOAD sem VMU. Fecha as dúvidas que dá para fechar sem cartão.
2. **Domínio nativo:** VMS, CRC, envelope e armazenamento atômico, com os unitários 1–6. Nada é
   instalado no jogo.
3. **Adaptador e slots** com `--saves nativo` opt-in e cartão vazio: init, os dois registros, mount,
   disk-info, exists e status/erro (unitários 7–8). O menu mostra 6 slots vazios, e o padrão `legado`
   fica byte a byte igual.
4. **SAVE de nome novo** e **LOAD** do mesmo arquivo depois de reiniciar o processo. É a primeira
   entrega funcional (um roteiro assistido curto). **Portão (achado do Codex):** só começa com um
   oráculo de *conclusão bem-sucedida*. O trace do passo 1 não tem cartão, então não mostra um sucesso.
   As opções são:
   - um harness controlado da biblioteca `bu` original (por exemplo, um cartão emulado só em
     desenvolvimento, para observar e não para entregar);
   - a análise completa do despacho interno até o callback.

   Em qualquer caso, o oráculo fixa a ordem callback/`C696`/`C112` e o `op` que chega para SAVE
   (`0x0C`) e LOAD (`0x0E`). A escolha entre as duas opções é do mantenedor, no início do N4.
5. Sobrescrita, cheio, somente leitura e recuperação; fechamento (`BS_BU_FECHAMENTO`); regressão.

Mudança em relação à ordem anterior: o LOAD de uma imagem conhecida (antigo incremento 5) entra junto
com o SAVE no passo 4. Sem VMU real, não há imagem conhecida de origem confiável. A primeira imagem
válida é a que o próprio jogo grava, e o oráculo do passo 2 garante o formato.

## Investigação do oráculo de SAVE/LOAD (2026-10-19, só leitura)

Autorização do mantenedor: só investigação e planejamento. Nada foi executado no Flycast, porque a
regressão do N3 estava rodando. Nenhum código foi alterado.

### O que já existe na máquina (conferido)

- `C:\Source\flycast-oracle\build\flycast.exe`, compilado em 13/10 com o servidor GDB. `emu.cfg`:
  - interpretador (`Dynarec.Enabled = no`);
  - `Debug.GDBEnabled = yes` e `GDBWaitForConnection = yes`;
  - BIOS HLE (`UseReios = yes`): **não precisa de BIOS real**;
  - `device1.1 = 10`, uma **VMU no slot A1**.
- `build/data/T13001N_vmu_save_A1.bin` (128 KB, por jogo): **formatada** (raiz `0x55`, FAT no bloco
  254, diretório 253 com 13 blocos), **200 blocos livres, nenhum arquivo**.
- O Flycast grava cada `MDCF_BlockWrite` direto no arquivo (`core/hw/maple/maple_devs.cpp`, `fwrite`).
  O arquivo depois de um SAVE é a prova de persistência.
- `tools/dev/flycast_oracle.py` (cliente GDB, MIT, processo externo): breakpoints, leitura de memória,
  `set_reg`, snapshot. **Falta** a escrita de memória (pacote `M`) e um modo de trace por endereço com
  retorno.

### Como o jogo chega ao SAVE e ao LOAD

- **Fluxo:**
  - controlador de save/load `8C01D200` (modo em `[8C01D244]`, ramos `8C01D3A0` e `8C01D570`);
  - painel `8C01E580` ("SAVE PANEL");
  - funções `FI_SLOT_Check` (`8C01DD1C`), `FI_FILE_Load` (`8C01DDBC`), `FI_VMS_Save` (`8C01DE30`) e
    `FI_VMS_Load` (`8C01DE7C`), chamadas por tabela (literais `8C01DD48`–`8C01DED8`);
  - máquinas com polling: `8C01CB18`, `8C01CCE0` (SAVE) e `8C01CE00` (LOAD).
- **Três pontos de chamada de `8C01D200`:**

  | Ponto | Argumentos | Contexto |
  |---|---|---|
  | `8C052694` (`8C052240`) | `(0, 8)` | gameplay; função que lê `on` e `press` (N3-0) |
  | `8C07BD0A` (`8C07BC2C`) | `(0, 8)` | outra tela do jogo, a identificar |
  | `8C0792C8` (`8C078FC0`) | `(1, 8)` | título, LOAD GAME ("ILLEGAL DATA ERROR") |

- **Hipótese H1 (não verificada):** `r4` escolhe o modo, 0 = SAVE (no jogo) e 1 = LOAD (título). Se
  valer, o oráculo força o painel de SAVE **a partir do título**, trocando `r4` para 0 na chamada de
  `8C0792C8`, sem atravessar o gameplay até um ponto de save.
  - Contra: a cabeça de `8C01D200` lê o modo de `[8C01D244]`, não de `r4`, que pode ser consumido por
    outra rotina.
  - O primeiro passo da captura confere H1. Se ela cair, valem os fallbacks da seção "Passos".

### As duas alternativas (da seção "Menor sequência", passo 4)

| | **A. Harness com a biblioteca `bu` original** | **B. Análise do despacho interno** |
|---|---|---|
| Como funciona | **A1:** o jogo original no Flycast, com a VMU emulada dele; o oráculo GDB registra a API `bu` e os callbacks. **A2:** uma VMU Maple só de desenvolvimento no nosso runtime, com a `bu` recompilada | ler `8C08BF50` (servidor), `8C090xxx` (fila e estado) e o caminho até `8C01C2D2`, e deduzir ordem, códigos e progresso |
| Ferramentas | A1: o que já existe e mais o trace `bu` no oráculo (escrita de memória, breakpoint de retorno); Python para ler a VMU. A2: código novo no runtime (Maple, função de armazenamento) | `bs-disasm`, `callgraph.tsv` e o Codex |
| Evidência | sequência real de chamadas, argumentos e retornos; ordem callback × `C696` × `C112`; `op` real de SAVE e LOAD; número de consultas; **arquivo VMS real** na VMU; persistência entre processos | hipóteses estáticas; sem arquivo, sem retorno real, sem tempo |
| SAVE e LOAD com sucesso | **sim** (A1: VMU real do emulador; A2: VMU nossa) | **não**: só descreve o caminho |
| Limitações | A1: Flycast lento no interpretador; a entrada precisa ser injetada; a VMU do Flycast não é a de hardware (o tempo do Maple é dele); somente leitura não é emulável. A2: **circular**: a VMU seria nossa, então valida a `bu` contra o nosso modelo. Acrescenta Maple, o que contraria a meta `Maple → 0`, e é implementação de runtime (não autorizada) | caminho longo (fila, servidor, Maple) com chances altas de erro de leitura; nada vira dado de teste |
| Tempo | A1: ~0,5 dia de ferramenta, 1–2 h de capturas, ~2 h de extração e validação. A2: 2–3 dias, e ainda precisa do A1 para validar | 1–3 dias, com resultado incompleto |
| Automação e reuso | A1: uma linha de comando refaz a captura; TSV e VMS viram fixtures dos unitários do N4 (fora do git, SKIP sem eles, como `BS_DISC_IMAGE`) | baixa |

**Recomendação: A1 (Flycast com VMU)**, com B só como apoio pontual para explicar o que o trace
mostrar. A2 fica descartada como oráculo: no máximo serviria de ferramenta de teste depois, sem valor
de referência.

### Plano de captura (A1)

Instrumentação mínima, só em `tools/dev` e fora do produto; a escrever quando autorizado. Um modo
`bu-trace` no `flycast_oracle.py` com:
- breakpoints nas **entradas** das 14 funções `bu` do inventário e nos dois callbacks;
- breakpoints nos **endereços de retorno** dos pontos de chamada do jogo (por exemplo `8C01CC48` e
  `8C01CD90`), para ler `r0` sem parar nas 8 consultas internas do servidor (`8C08BF96`);
- por evento, uma linha TSV:

  | Coluna | Conteúdo |
  |---|---|
  | `seq` | ordem do evento |
  | `vblank` | contador de VBlank do jogo, lido da RAM |
  | `pc`, `pr` | onde parou e quem chamou |
  | `r4`–`r7` | argumentos |
  | `pilha[0..1]` | argumentos 5 e 6 |
  | `r0` | valor de retorno, nos breakpoints de retorno |
  | `bloco` | os 184 bytes de `8C1F3710` (unidades 0 e 1) depois de cada callback |

- a entrada injetada por escrita no PDS do pd na saída de `8C15C4E6` (estrutura do
  `transitional/pad_contract`), com o mesmo roteiro por passo. É determinística e não depende de
  teclado nem de foco de janela.

Um script `n4_vmu.py` (Python, `-I`) lê a VMU 128 KB (FAT, diretório) e extrai cada `BLUESTIN.00n`:
- blocos e tamanho;
- os campos VMS da tabela;
- o CRC recalculado (1 840 bytes, `0x46` zerado);
- o payload comparado com a RAM `8C24F93C` capturada na chamada do SAVE;
- o SHA-256.

**O que se observa e como:**

| Item | Como |
|---|---|
| `0x0C` e `0x0E` | `r5` na entrada do despacho (`8C08C83A` e `8C08C8F6`, `jsr @r3`) e `op` no callback `8C01C2D2` |
| ocupado, concluído, erro | `r0` de `C696` em cada retorno, na sequência; `r0` de `C112` |
| callback | `op` e `arg` e a posição na sequência relativa ao primeiro `C696 = 0` |
| polling | número de `C696 = −1` por operação e VBlanks entre o pedido e a conclusão (só informativo: o adaptador escolhe um número dele, que precisa ser menor que 450) |
| retornos | `C818` e `C8D4` (0 aceito, −1 recusado), `C636` (existe ou `FF05`), `CA34` e `C1FC` |
| VMS | `n4_vmu.py` sobre a VMU depois do SAVE |
| persistência | encerrar o Flycast, abrir de novo com a mesma VMU, LOAD e comparar `8C24F93C` com o payload gravado |
| seis slots | um SAVE em cada `BLUESTIN.000`–`.005` (se o painel deixar escolher), varredura de `exists` com 0, 3 e 6 arquivos, sobrescrita de um deles |
| erros | VMU zerada (não formatada); VMU com blocos livres < 4 e com o diretório cheio, as duas preparadas offline por `n4_vmu.py` sobre uma cópia; sem VMU (`device1.1 = 0`); CRC do VMS adulterado na cópia e LOAD (responde quem valida o CRC) |

**O que precisa ser igual ao original:**
- os retornos de cada função `bu` que o jogo lê;
- os códigos de `op` e de erro;
- a ordem callback/`C696`/`C112`;
- o fato de haver ao menos uma consulta "ocupada" antes da conclusão, se o trace mostrar isso;
- os campos de `disk-info` que o jogo copia (`+34`, `+38`);
- os bytes VMS e a geometria do LOAD;
- os seis nomes.

**O que o `ISaveStore` abstrai no Windows:**
- o protocolo Maple e o tempo do barramento;
- a FAT e o diretório da VMU, e a alocação de blocos;
- o tempo em milissegundos;
- a pasta, o envelope, a atomicidade e o `.bak`;
- o LCD;
- o caso "somente leitura" (não existe na VMU; é decisão de produto mapeá-lo para o código de erro mais
  próximo que o trace mostrar).

**Sem dependência permanente:**
- o Flycast, a VMU e o Maple só aparecem no oráculo, em `tools/dev`;
- o produto e os unitários usam os **resultados** (TSV, VMS) gravados em `analysis/n4-oraculo/`, fora
  do git, e pulam (SKIP) sem eles;
- os unitários do domínio (VMS, CRC, envelope) rodam sem nenhum arquivo do oráculo, contra as rotinas
  puras recompiladas.

### Passos até o primeiro SAVE/LOAD com sucesso (depois da regressão do N3, com autorização)

1. Ferramenta: escrita de memória, `bu-trace` e injeção de entrada no oráculo; `n4_vmu.py`. Cópia de
   trabalho da VMU em `analysis/n4-oraculo/vmu/`: o original de 13/10 não é tocado, e o Flycast recebe a
   cópia.
2. **Trace do boot ao título e LOAD GAME** com a VMU vazia: init, os dois registros, montagem
   (`op 0x12` → `CA34` → `op 1`), `disk-info` e seis `exists = FF05`. Sem sucesso de SAVE ainda, mas é o
   primeiro oráculo positivo (cartão presente).
3. **H1:** na chamada de `8C0792C8`, trocar `r4` para 0. Se o painel de SAVE abrir, gravar um slot.
   - Fallback 3a: o mesmo efeito escrevendo o modo em `[8C01D244]`, com o valor tirado do trace do
     ponto `8C052694`.
   - Fallback 3b: chegar a um ponto de save real no gameplay com a entrada injetada, reaproveitando os
     roteiros por passo. É mais caro e o tempo do Flycast é outro.
4. **Persistência:** encerrar, reabrir com a VMU gravada, LOAD do slot e comparar o payload.
5. Seis slots, sobrescrita e os erros da tabela, cada um com uma VMU preparada.
6. Repetir 2–4 numa segunda execução independente (determinismo da sequência) e passar pela revisão do
   Codex.

**Ferramentas (preparadas em 2026-10-19, só `tools/dev`; nunca rodadas contra o Flycast):**

- `flycast_oracle.py bu-trace`. Os modos antigos ficaram intactos; o cliente GDB ganhou só a escrita
  de memória `M`.
  - Eventos no TSV: `entrada`, `retorno`, `callback`, `painel`, `forcado`, `ancora`, `marca`. Colunas:
    passo, ms, PC, PR, `r4`–`r7`, dois argumentos de pilha, `r0` e extra.
  - O retorno é capturado no PR de cada entrada, conferindo `r15`, o que cobre as chamadas de cauda.
  - O estado das unidades (184 bytes) é gravado na volta de cada callback de conclusão.
  - Despejos: a origem do SAVE (`r7·512`), o payload `8C24F93C` e o destino do LOAD (1 536 bytes).
  - Entrada pela porta A na volta do servidor `pd` (`8C1730BE`), escrevendo `on`, `off`, `press` e
    `release`.
  - O roteiro aceita `@ancora <end>`, que realinha os passos a um ponto do jogo, e
    `@pulsos <estado> <n>`.
  - `--forcar-modo-save <jsr>` testa H1. `--marcas` registra o primeiro passo de cada endereço.
  - `--vmu`/`--vmu-saida` trocam a VMU do Flycast pela cópia da sessão, e a original volta no fim.
  - **Encerramento por `WM_CLOSE`:** a VMU do Flycast é gravada com `fwrite` sem `fflush`
    (`maple_devs.cpp`) e só chega inteira ao disco no `fclose`. Se o Flycast precisar ser morto, o
    comando sai com código 3 e avisa que a VMU pode estar incompleta.
- `n4_vmu.py`: lê VMU (FAT, diretório, cadeias), extrai arquivos, valida VMS (tamanho, blocos,
  campos, CRC, zeros, payload esperado), cria VMU vazia e VMS sintético, e prepara VMUs de erro
  (`--livres N`, `--zerada`, `--adultera-crc`). O `teste` roda 12 verificações só com amostras
  sintéticas, incluindo o vetor conhecido do CRC-16/XMODEM (`"123456789"` → `31C3`), e passa.
- `roteiros_n4/painel_a.txt`: o prefixo do `passo_load_opcoes` até o LOAD GAME, a âncora no painel e
  A a cada 90 passos. O mesmo roteiro serve ao LOAD e, com `--forcar-modo-save`, ao SAVE.
- Conferido sem o Flycast:
  - compila;
  - roteiro, âncora, pulsos e bordas com um GDB falso (`tools/dev/teste_bu_trace.py`). A entrada é
    escrita em todo passo, inclusive no neutro, para o controle do Flycast não vazar para o PDS;
  - VMU e configuração trocadas dentro de um único `try/finally` e restauradas em qualquer saída;
  - `--attach` recusado junto com `--vmu`/`--sem-vmu`.

  A VMU do
  Flycast (13/10) é lida corretamente. Ela tem restos nos blocos 0 e 11, fora de qualquer arquivo, e
  por isso a base das capturas é **uma cópia dela**, não a `nova`.

**Comandos da captura** (depois da regressão do N3; `A` = `analysis/n4-oraculo`):

```
copy C:\Source\flycast-oracle\build\data\T13001N_vmu_save_A1.bin A\vmu\vazia.bin
rem c0: calibracao (sem forcar nada): primeiro passo do titulo, do painel e dos pontos do jogo
python tools/dev/flycast_oracle.py bu-trace A\c0.tsv --vmu A\vmu\vazia.bin --roteiro tools/dist/roteiros/passo_load_opcoes.txt --passos 2500 --marcas 8C078FC0,8C07BC2C,8C052240,8C01D200
rem c1: LOAD GAME com a VMU vazia (montagem, disk-info, seis exists)
python tools/dev/flycast_oracle.py bu-trace A\c1.tsv --vmu A\vmu\vazia.bin --vmu-saida A\vmu\c1.bin --roteiro tools/dev/roteiros_n4/painel_a.txt --passos 4000
rem c2: SAVE forcado pelo titulo (H1)
python tools/dev/flycast_oracle.py bu-trace A\c2.tsv --vmu A\vmu\vazia.bin --vmu-saida A\vmu\c2.bin --dump A\c2 --roteiro tools/dev/roteiros_n4/painel_a.txt --forcar-modo-save 0x8C0792C8 --fim-apos 1
python -I tools/dev/n4_vmu.py extrai A\vmu\c2.bin A\c2\vmu
python -I tools/dev/n4_vmu.py valida A\c2\vmu\BLUESTIN.000 --bluestinger --payload A\c2\<seq>_payload.bin --json A\c2\vms.json
rem c3: persistencia: processo novo, LOAD do slot gravado
python tools/dev/flycast_oracle.py bu-trace A\c3.tsv --vmu A\vmu\c2.bin --vmu-saida A\vmu\c3.bin --dump A\c3 --roteiro tools/dev/roteiros_n4/painel_a.txt --fim-apos 1
rem erros: cada VMU preparada passa por um c2 (SAVE) ou c3 (LOAD) com --fim-apos 1
python -I tools/dev/n4_vmu.py prepara A\vmu\vazia.bin A\vmu\cheia3.bin --livres 3
python -I tools/dev/n4_vmu.py prepara A\vmu\vazia.bin A\vmu\zerada.bin --zerada
python -I tools/dev/n4_vmu.py prepara A\vmu\c2.bin A\vmu\crc_ruim.bin --adultera-crc BLUESTIN.000
python tools/dev/flycast_oracle.py bu-trace A\e_cheia.tsv --vmu A\vmu\cheia3.bin --roteiro tools/dev/roteiros_n4/painel_a.txt --forcar-modo-save 0x8C0792C8 --fim-apos 1
python tools/dev/flycast_oracle.py bu-trace A\e_zerada.tsv --vmu A\vmu\zerada.bin --roteiro tools/dev/roteiros_n4/painel_a.txt --passos 4000
python tools/dev/flycast_oracle.py bu-trace A\e_crc.tsv --vmu A\vmu\crc_ruim.bin --dump A\e_crc --roteiro tools/dev/roteiros_n4/painel_a.txt --fim-apos 1
python tools/dev/flycast_oracle.py bu-trace A\e_sem.tsv --sem-vmu --roteiro tools/dev/roteiros_n4/painel_a.txt --passos 4000
```

Matriz de erros e como cada um é produzido:

| Erro | Como |
|---|---|
| sem cartão | `--sem-vmu`: `device1.1 = 0` só na sessão, e o `emu.cfg` volta no fim |
| não formatado | `prepara --zerada` |
| não encontrado | c1 (VMU vazia: `exists` dos seis nomes) |
| cartão cheio | `prepara --livres 3` e SAVE |
| CRC adulterado | `prepara --adultera-crc` e LOAD: responde se a `bu` valida o CRC |
| diretório cheio | **inalcançável na VMU padrão**: 208 entradas para 200 blocos, então os blocos acabam antes. Só existiria com uma imagem fora do padrão; fica fora da matriz |
| somente leitura | não existe na VMU nem no Flycast; decisão de produto (seção "Plano de captura") |

O passo do c0 calibra o prefixo do `painel_a.txt`, porque o boot do Flycast pode durar outro número de
passos. O c1 diz se a âncora `8C01D200` e os pulsos de A abrem o soquete e os slots.

### Estimativa e riscos

- **Tempo:** ~1 dia de trabalho, mais 1–2 h de capturas no Flycast. Os fallbacks 3a e 3b podem
  acrescentar de 0,5 a 1 dia.
- **Riscos:**
  - H1 falha: há os fallbacks 3a e 3b;
  - payload de título não inicializado: o protocolo e o VMS continuam válidos como oráculo, mas o
    conteúdo do jogo vem depois, com o SAVE real no nosso runtime;
  - lentidão do interpretador do Flycast combinada com as paradas do GDB: breakpoints só em ~20
    endereços, nunca dentro do laço do servidor;
  - perda de aviso de parada do stub GDB (conhecida, já tratada pelo `cont()`);
  - a VMU do Flycast difere do hardware em tempo: tempo é abstraído, só a ordem e os retornos contam;
  - somente leitura não é capturável.

### Critérios para considerar o oráculo validado

1. Duas execuções independentes com **a mesma sequência normalizada**: cada série de
   `status = FFFFFFFF` consecutivos vira um só evento. Fora disso, ficam iguais a ordem, os argumentos
   e os retornos de todos os demais eventos e o primeiro `status = 0`. O tamanho de cada série
   (número de consultas "ocupada") fica registrado e pode variar.
2. Pelo menos um SAVE (`0x0C`) e um LOAD (`0x0E`) concluídos com sucesso, com a ordem
   callback/`C696`/`C112` e o código de sucesso observados.
3. VMS extraído da VMU:
   - 4 blocos;
   - campos da tabela;
   - CRC recalculado = CRC gravado = rotina pura recompilada (`8C08CDE2`);
   - payload = RAM `8C24F93C` no SAVE;
   - `n4_vmu.py valida --bluestinger` passa: um ícone, sem eyecatch, 1 200 bytes e as duas descrições
     do jogo.
4. Persistência: um processo novo do Flycast acha o slot (`exists`) e o LOAD entrega o payload em
   `dest + 0x80`.
5. Os erros da matriz capturados com o código de cada um: sem cartão, não formatado, não encontrado,
   cheio, CRC adulterado. "Diretório cheio" e "somente leitura" ficam marcados como não capturáveis,
   com o motivo.
6. Resultados em `analysis/n4-oraculo/` com o SHA-256 do `1ST_READ.BIN` e das VMUs, refeitos por
   comando; revisão do Codex sem achado alto em aberto.

### Revisão do Codex das ferramentas (`reviews/codex-20261019-015100.md`)

Somente leitura (rodou só o `n4_vmu.py teste`). Todos os achados foram conferidos no código e aceitos.

| Achado | Decisão |
|---|---|
| [MÉDIA] o estado neutro não sobrescrevia o PDS: o controle do Flycast vazava | aceito: escrita em todo passo |
| [MÉDIA] a troca da VMU ficava fora do `try/finally`; `--attach` copiaria a VMU antes do `fclose` | aceito: troca dentro do `try`, restauração em qualquer saída, `--attach` recusado com `--vmu` |
| [MÉDIA] a matriz de erros não era produzível (sem cartão, diretório cheio) | aceito: `--sem-vmu` (com `emu.cfg` restaurado); diretório cheio documentado como inalcançável (208 entradas para 200 blocos) |
| [BAIXA] o `valida` aceitava VMS fora do formato do jogo | aceito: `--bluestinger` (perfil estrito) e eyecatch ≠ 0 recusado no modo genérico |
| [BAIXA] o critério 1 era contraditório (consultas variáveis × "mesma sequência") | aceito: sequência normalizada |

Confirmados: `Gdb.write`; o ponto `8C1730BE`; o PR `8C0792CC` do H1; os retornos de `C696` lidos antes
do uso; as chamadas de cauda por PR e `r15`; a remoção de breakpoints; modos antigos intactos;
geometria VMU; CRC no binário; autoteste.

**Não verificado** (só a captura real resolve): o stub GDB sob carga; o `WM_CLOSE` acionando o `fclose`
da VMU; todo o conteúdo de c1–c3.

## Oráculo SAVE/LOAD no Flycast: resultados (2026-10-19)

Dados em `analysis/n4-oraculo/` (fora do git): TSV, despejos e VMUs. A VMU original de 13/10 não foi
tocada (SHA-256 `B4C5A531…` antes e depois), e o `emu.cfg` foi restaurado em todas as sessões.

**Correção da configuração.** O `emu.cfg` de 14/10 tem `device1.1 = 10`, que no enum real
(`core/hw/maple/maple_cfg.h`) é `MDT_None`, e não VMU. No c0 o jogo não via cartão: só init e
registros, sem `op 0x12`. O `bu-trace` agora grava `device1.1 = 1` (`MDT_SegaVMU`) com `--vmu` e
`device1.1 = 10` com `--sem-vmu`, só na sessão. Antes, o `--sem-vmu` gravava 0, um controle. A
âncora do `painel_a.txt` mudou de `8C01D200` para `8C0792C8`: `8C01D200` também é chamado no boot
(`8C07848A`), e isso adiantava os pulsos.

**Evidência dinâmica do cartão (c1, VMU vazia), no boot e antes de qualquer menu:**

| # | Evento | Valores |
|---|---|---|
| 1 | `init(0x7A, 3, 0, 8C01C37A)` | 0; registra os callbacks `8C01C2D2` e `8C01C364` |
| 2 | callback de conclusão `(0, 0x12, 0x0F)` | inserção, 15 blocos de `work` |
| 3 | progresso `(0, 0x12, 1..2, 0)` | |
| 4 | `mount(0, work, 0x1E20)` | 0; `0x1E20` = 15·512 + 32, como o callback calcula |
| 5 | progresso `(0, 1, n, 0x11)` | |
| 6 | callback `(0, 1, 0)` | montado; dentro dele `disk_info(0, 8C1F3724)` = 0 |
| 7 | `exists` × 6 (`8C0BC6E0`, o nome reescrito a cada slot) | `FFFFFF05` |
| 8 | `status(0)` e `ultimo_erro(0)` | 0 e 0 |

**SAVE com sucesso (c2, H1 confirmada).** `r4` passa a 0 na chamada `8C0792C8` do título e abre o
painel de SAVE:
- `vms_blocos` e `vms_monta(img, desc, payload)` devolvem 4 blocos; o CRC fica nos 16 bits baixos do
  `r0`.
- `save(0, "BLUESTIN.000", img, 4, time*, 0x80000000)` devolve **0**.
- **11 × `status = −1`** (um por passo de jogo) e depois o **callback `(0, 0x0C, 0)`**.
- Só **depois** disso, `status = 0`, sem `ultimo_erro` na sequência do SAVE.
- O próprio jogo então confere com um LOAD: `load(0, nome, 8C1F385C, 1, 1)` devolve 0; o callback
  `(0, 0x0E, 0)` vem no mesmo passo e `status = 0` no passo seguinte. É o segundo uso de LOAD
  (`8C01CB18`) citado no inventário: leitura do bloco 1 para conferência.

**VMS no cartão** (`n4_vmu.py`): `BLUESTIN.000`, 4 blocos (199 em diante), 196 livres.
- `valida --bluestinger --payload`: **VÁLIDO**. "BlueStinger" / "BLUESTINGER SAVE DATA", 1 ícone,
  animação 8, eyecatch 0, CRC `BEDB` = calculado, tamanho 1 200.
- Payload = RAM `8C24F93C` no SAVE; arquivo = origem do SAVE, byte a byte. O destino do LOAD de
  conferência = bytes 512–1023 do VMS.
- SHA-256 `a13a1266dabf0689…`.

**Contrato observado para o adaptador:**
- `op` do SAVE = `0x0C` e do LOAD = `0x0E`; eles chegam ao callback como `op`, com `arg = 0` no
  sucesso;
- callback de conclusão **antes** do primeiro `status = 0`;
- ao menos uma consulta ocupada no SAVE (11 no Flycast);
- `ultimo_erro` não é lido no sucesso.

**Persistência (c3/c3b, processo novo com a VMU do c2):**
- `exists` acha `BLUESTIN.000` (0) e devolve `FF05` para os outros cinco;
- o jogo faz duas conferências do bloco 1 e depois o **LOAD completo**:
  `load(0, nome, 8C24F8BC, 1, 0)` devolve 0;
- progresso `(0x0E, 1..3, 3)`, um `status = −1`, conclusão `(0, 0x0E, 0)` e depois `status = 0`;
- destino = bytes 512–2047 do VMS, e **destino + 0x80 = payload gravado no c2**.

**Progresso por operação** (o mesmo padrão em todas): `progresso(drive, op, k, total)` para k = 1..n,
depois a conclusão.

| Operação | Progresso | Conclusão |
|---|---|---|
| inserção | `0x12`, 1..2, total 0 | `0x12`, arg 15 |
| montagem | `1`, 1..16, total `0x11` | `1`, arg 0 |
| SAVE de 4 blocos | `0x0C`, 1..10, total 10 | `0x0C`, arg 0 |
| LOAD | `0x0E`, 1..n, total = blocos lidos | `0x0E`, arg 0 |

**`disk_info`** (56 bytes no bloco do jogo `+20`):
- cor `01 FF FF FF 64`;
- `+32` 256 blocos, `+34` 200 de usuário, `+36` e `+38` livres (200, e 196 depois do SAVE);
- `+40` 128, `+42` 128, `+44` 512, `+46` 5;
- `+48` a data de formatação (1998-11-27, 00:00:59, dia 5).

O jogo copia os de usuário e os livres (`8C01C8CE`).

Pendentes: a repetição de c1–c3 (critério 1) e a matriz de erros.

## SAVE e LOAD de gameplay (2026-10-19)

**Causa do "This save data is corrupt".** A máquina de LOAD do jogo (`8C01CE00`) só termina em 10
(falha) por timeout, depois de 450 consultas ocupadas. Com o `status` pronto ela devolve 8 (sucesso), seja
qual for o conteúdo. A recusa vem da verificação do conteúdo, feita depois. O save do título (H1) não
tem estado de gameplay. Um save feito no gameplay carrega normalmente nos dois ambientes (abaixo).

**Como o jogo abre o SAVE no gameplay** (bs-disasm):
- `8C0407C0` (tratador de A) chama `8C04FB80(jogador, sp, 0)`, que procura um ator à frente na tabela
  de atores (`[8C24F858]`, contagem `[8C24F86C]`, 36 B por ator, objeto em `+24`, kind em `objeto+2`);
- devolve **4** quando o kind tem o bit `0x80` na tabela de kinds `8C14CFE0` (32 B por kind). Só o
  **kind 98** tem esse bit (o 97 divide o tratador `8C0721C0`, sem o bit);
- o tipo 4 grava **7** em `[8C24D2CC+0x25B2]` (`8C0409EE`). O laço `8C052240` passa a **8** e chama
  `8C01D200(0, 8)`, o painel de SAVE.
- Nenhuma sala das rotas atuais (RCG00, RCG00A, RCG02, RCG03) tem um ator kind 98. O save point real
  depende de avançar a campanha (porta C01 com cartão de ID, monstro do heliporto).

**Diagnóstico `BS_SAVES_H2=<passo>`** (build com mods, `--saves nativo`, só desenvolvimento): a partir
desse passo de jogo, A sem nada à frente faz `8C04FB80` (modo 0) devolver 4. **Só a presença do save
point é simulada.** O resto é o fluxo do jogo: pergunta, confirmação, painel e SAVE com o estado real
da partida. Antes do passo indicado nada muda, porque os roteiros apertam A em portas.

**Resultado (sessão assistida, `passo_cgate` até o passo 8003, corredor RCG02):**
- **SAVE no Windows:** painel do gameplay, Socket 1, slot 1, "Do you want to Save here?" e
  "Save completed.". Grava `card0\BLUESTIN.000.bsv` de 2 080 B. O VMS é **válido**: CRC `2835`
  igual ao calculado, 1 200 B de payload, SHA-256 `1644836c04cc450f…`.
- **LOAD no Windows (processo novo):** LOAD GAME lista "ELIOT 00:03:58, $0" (o local aparece como
  "????????": o corredor não é sala de save point). A confirmação **carrega sem erro**: sala `RCG02`,
  Eliot em (126.9, 0.0, −560.4), a mesma posição do SAVE, e a partida continua jogável.
- **LOAD no Flycast** (`g1`; o VMS gravado numa VMU por `n4_vmu.py insere`):
  - duas conferências do bloco 1 e o LOAD completo;
  - destino = `VMS[512:2048]` byte a byte;
  - 900 passos depois, Eliot em **(126.85, 0.0, −560.42)**, a mesma posição;
  - estado `00` (gameplay livre);
  - a VMU sai inalterada.
- **Conclusão:** o save do provedor nativo é legível pelo jogo original no Flycast e retoma a partida
  nos dois ambientes.

Ferramentas novas:
- `n4_vmu.py insere`;
- `flycast_oracle.py bu-trace --ler-fim end:n,...`;
- `assistido.py iniciar --extra "<args>"`.

Pendentes do N4 nesse momento (situação no fechamento, em "Encerramento do N4" abaixo):
- save point real (kind 98) alcançado sem H2: **continua pendente**;
- SAVE de gameplay também no Flycast, comparado ao do nativo: feito (`f2`, seção seguinte);
- matriz de erros: feita (robustez, seção seguinte);
- revisão do Codex: feita (`STATUS.md`);
- regressão de fechamento: feita (`STATUS.md`, fonte `C29EFADBDD4305BD`);
- promoção a padrão: feita (`--saves nativo`).

## Compatibilidade nos dois sentidos, robustez e promoção (2026-10-19)

**Flycast → Windows** (`f2`, jogo original):
- Roteiro `tools/dev/roteiros_n4/save_gameplay_a.txt`:
  - NEW GAME;
  - âncora no primeiro passo de gameplay (`8C0407C0`), porque o Flycast conta outro número de passos
    até lá: 8 343 contra cerca de 2 255 no recompilado;
  - A a cada 50 passos de +450 a +700 (o A só age uns 400 passos depois do início do gameplay);
  - âncora no painel e pulsos de A.
- O único desvio é `bu-trace --forcar-save-point`: uma vez, em `8C0408EA`, o `r0` passa de 0 a 4,
  o mesmo que o `BS_SAVES_H2`. O resto é o jogo original.
- Resultado:
  - `save` com op 0x0C e conclusão 0, no cais RCG00, Eliot em (500, 92, −650), vida 40;
  - VMS **válido**, igual à origem do SAVE byte a byte (SHA-256 `92474e6e…`).
- `--saves-importar` e LOAD em processo novo (`test_saves` S12):
  - sem erro, Eliot em **(500, 92, −650), vida 40**;
  - a partida continua jogável (anda até (527, −378)).

**Windows → Flycast:** feito antes (`g1`, seção anterior), com o save de gameplay do RCG02.

**As duas condições não são a mesma partida.** Os saves vêm de pontos diferentes: RCG02 no Windows,
RCG00 no Flycast. Cada sentido foi provado com o seu próprio save. Nos dois o local é simulado: só a
presença do save point.

**Robustez** (unitários e `test_saves`):
- inexistente;
- envelope corrompido (preservado como `.corrompido`);
- CRC do VMS inválido (recusado na gravação e na importação; o save anterior fica);
- versão futura, inclusive com cabeçalho curto (preservada, nunca sobrescrita);
- ocupada (segunda operação recusada com −1);
- concluído dentro do limite de 450 consultas;
- unidade ocupada durante os progressos e liberada antes da conclusão;
- falha em cada ponto da escrita atômica;
- sem espaço;
- somente leitura respeitado;
- `.tmp` descartado e recuperação pelo `.bak`;
- persistência entre processos;
- FF03 em cartão não formatado.

**Promoção:**
- `--saves nativo` passa a ser o padrão; `--saves legado` fica como fallback temporário.
- No `--headless` sem pasta, o cartão é temporário e vazio por processo: os testes nunca tocam
  Documentos.
- `--saves-importar` e `--saves-exportar` levam e trazem VMS entre o produto e VMU ou emulador.

**Pendente: o save point real.** As salas que as rotas alcançam (RCG00, RCG00A, RCG02, RCG03) não
têm ator kind 98. Chegar a um exige avançar a campanha (cartão de ID da porta C01, monstro do
heliporto). O critério "SAVE num save point real" segue **pendente**; mudá-lo exige aprovação do
mantenedor.

## Piloto Ghidra (2026-10-19)

Ghidra 12.1.4 portátil com o JDK 21 (Temurin 21.0.12.1), os dois em `C:\Source\_tools`, sem PATH,
JAVA_HOME ou registro globais (só no processo). SHA-256 conferidos com a release e com a Adoptium.

- **Import:** `analyzeHeadless ... -import game-data\1ST_READ.BIN -loader BinaryLoader
  -loader-baseAddr 0x8C010000 -processor SuperH4:LE:32:default`; análise automática em ~2 min. O
  mapeamento é o mesmo do `bs-disasm` (base `0x8C010000`).
- **Script:** `analysis/ghidra-piloto/scripts/BsDecompila.java` (fora do git): para cada endereço,
  cria a função se faltar e grava os chamadores, as chamadas e o pseudocódigo C.
- **As três funções do piloto,** comparadas com o `bs-disasm` e com este documento:
  - `8C01C2D2`, o callback de conclusão: os quatro ramos (`op` 1, 2, `0x12` e outro), os offsets
    `+0/+2/+4/+16/+20/+76/+80/+84/+88` e `+16 ← 0` no fim, iguais à tabela do contrato. O
    pseudocódigo mostra o truncamento `(drive * 0x5C) & 0xFF` explicitamente.
  - `8C08CDE2`, o CRC: polinômio e estado inicial iguais. O byte é lido com sinal (`mov.b`), mas
    `sext(b) << 8` só muda os bits acima de 15, e o deslocamento é só para a esquerda. Os 16 bits
    baixos são o CRC-16/XMODEM de `saves_format.cpp`.
  - `8C08BEBC`, o exit: nenhum chamador, só o literal. Antes de desregistrar o servidor
    (`8C17C2D0`), consulta as 8 unidades e chama `8C090802`/`8C091132`. Isso detalha a descrição do
    inventário, que citava só o desregistro.
- **API `bu` lida de uma vez** (`analysis/ghidra-piloto/bu_api.txt`, 18 funções em ~30 s):
  - LOAD `C8D4`, SAVE `C818` e `C7BE` têm a mesma forma: confere a unidade
    (`8C08CA18(drive, blocos)`), grava a operação (`0x0E`, `0x0C` ou `0x0B`) e os parâmetros e
    devolve 0 ou −1. O SAVE ainda grava o ponteiro de tempo (`8C08CA28`). `C7BE` é op `0x0B`, com
    papel ainda não verificado.
  - `C696`, `C112`, `C1FC` e `C636` começam todos por `8C090BA4(drive)`, a verificação de unidade,
    e devolvem −1 se ela falhar.
  - O wrapper de SAVE do jogo, `8C01C520`, monta o descritor VMS na pilha:
    - textos "BlueStinger" e "BLUESTINGER SAVE DATA";
    - 1 ícone, velocidade 8, eyecatch 0;
    - o tamanho vem de `8C08CC58(1, 0, tamanho)`;
    - aloca `blocos << 9` bytes e monta a imagem com `CA5C`;
    - a data é preenchida por `8C01C2A0`;
    - chama o SAVE com a flag `[8C01C664]`;
    - o retorno é comparado com as quatro constantes de erro, com as mensagens
      `buSaveFile NO_DISK/UNFORMAT/NOT_FOUND/CANNOT_OPEN/UNKNOWN`.
  - `8C01C8CE` (exists dos seis nomes): marca 1 ou 0 em `tabela + drive·12 + 6 + i` e grava a
    contagem em `+4`. Copia `+0x3A` e `+0x36` do bloco de 92 bytes devolvido por `8C01C4A0`.

**Conclusão:** o Ghidra é útil e passa a ser a ferramenta de leitura do N4. Ele dá pseudocódigo
correto e xrefs com o tipo (chamada, `DATA` e `PARAM`, este último pegando literais que a ferramenta
de fronteira não vê), e lê dezenas de funções em segundos. Não substitui o `bs-disasm` nem o oráculo:
nomes, `op` e retornos continuam valendo só depois do trace do Flycast.

O REA (MCP) fica para depois. Ele exigiria instalar o pacote npm e mudar a configuração global do
Codex e do Claude Code, o que precisa de aprovação do mantenedor, e o ganho sobre este script
headless é pequeno para o N4.

## Contrato de implementação (preparado em 2026-10-19, para começar o N4 sem nova investigação)

### `ISaveStore`: mudanças mínimas

Hoje (`native/platform/saves.h`) a interface tem `present`, `free_blocks`, `list`, `load`,
`save(SaveInfo, dados)` e `remove`, é síncrona, e **nenhum código a usa nem a implementa**. Por isso a
mudança não quebra nada.
- **A assincronia não entra no `ISaveStore`.** Ela fica no `transitional/bu_adapter`, no mesmo padrão
  do `gdfs_adapter` do N1: o pedido é aceito, publicado como ocupado e concluído numa consulta
  posterior. O store continua síncrono e é chamado pelo adaptador **no instante da conclusão**, na
  thread do jogo. Quando `C696` passa a 0, o arquivo já está durável. Um SAVE de 2 KB com flush leva
  milissegundos; o número de consultas "ocupada" é fixo e não depende do tempo de disco, então o jogo
  continua determinístico.
- **Trocas na interface:**

  | Hoje | Proposta | Motivo |
  |---|---|---|
  | `present` + `free_blocks` | `Result<CardState> card(int unit)`: presente, formatado, somente leitura, blocos e entradas totais e livres | `disk-info` precisa de tudo de uma vez (unitário 9) |
  | `SaveInfo{comment, format_version}` | `SaveEntry{name, blocks, modified_unix}` | o comentário mora no cabeçalho VMS; a versão é do envelope, interna ao store |
  | `load` devolve bytes | devolve os 2 048 bytes VMS (`SaveImage`) | o adaptador corta o bloco inicial que o jogo pede |
  | `save` | `save_atomic(unit, name, imagem 2 048)`: só retorna ok com o arquivo durável | contrato de durabilidade |
  | `remove` | mantido | delete pelo jogo não verificado |

- **Erros:** o `Error` global não distingue ausente, não formatado, cheio, sem entradas, somente
  leitura nem versão futura. Opções:

  | Opção | Vantagem | Risco |
  |---|---|---|
  | **(a)** um `enum class SaveError` próprio em `saves.h` | não toca outros subsistemas; o mapeamento para `FF01..` fica completo no adaptador | um segundo tipo de erro no `native/` |
  | (b) estender o `Error` global com 5 valores | um tipo só | toca `error_name` e quem faz `switch` no `Error` (N1–N3) |

  Recomendação: (a).

### Unitários (todos em `bs_unit_tests`, sem disco do jogo nem gameplay)

Os unitários 1–11 acima continuam. Mapa para os casos pedidos, com o que faltava:

| Caso | Unitário |
|---|---|
| SAVE e LOAD | 1–3, 7 e 11 (replay do oráculo) |
| persistência | 10 (nova abertura do store em pasta temporária) |
| arquivo inexistente | 8 (`exists`/LOAD → `FF05`) |
| arquivo corrompido | 4 (envelope: magic, versão, tamanho, checksum, truncado) e LOAD de VMS com CRC ruim, que segue a política do oráculo (e_crc) |
| operação ocupada | 7 (segunda operação recusada, pendente preservado) |
| **timeout** (novo, 12) | o adaptador conclui em K consultas, com 1 ≤ K < 450, para qualquer operação, inclusive com erro. Um store que falha devolve conclusão com erro, nunca "ocupado" para sempre |
| **falhas de escrita** (novo, 13) | ganchos de falha do store (`FaultPoint`: criar temporário, escrever, flush, reler, trocar) só nos testes. Para cada ponto, o save anterior fica intacto e o adaptador conclui com o erro mapeado |

O gancho de falha é um parâmetro de construção da implementação (um `std::function`) e não entra na
interface. Não cria framework novo: os testes usam o `bs_unit_tests` existente e uma pasta temporária.

### Escrita atômica no Windows

Destino: `<cartão>\BLUESTIN.00n.bsv`.
1. Escrever o envelope inteiro em `BLUESTIN.00n.bsv.tmp` na **mesma pasta** (`CreateFileW`,
   `WriteFile`, `FlushFileBuffers`, `CloseHandle`).
2. Reabrir o temporário, reler e validar o envelope (magic, versão, tamanho, checksum, CRC do VMS).
3. Trocar:
   - sem destino: `MoveFileExW(tmp, destino, MOVEFILE_WRITE_THROUGH)`;
   - com destino: `ReplaceFileW(destino, tmp, BLUESTIN.00n.bsv.bak, REPLACEFILE_IGNORE_MERGE_ERRORS)`.
     Alternativa, se o `ReplaceFileW` falhar com os códigos de "troca parcial": copiar o destino para
     `.bak` com flush e depois `MoveFileExW(tmp, destino, MOVEFILE_REPLACE_EXISTING |
     MOVEFILE_WRITE_THROUGH)`.
4. Só então concluir para o jogo.

**Na abertura do cartão:**
- `.tmp` é sempre descartado. O jogo não viu aquele SAVE concluído, então ele não existiu. Essa é a
  regra que impede um save "fantasma".
- destino válido: usa o destino;
- destino ausente ou inválido com `.bak` válido: recuperação pela política decidida (pendência 3);
- os dois inválidos: o slot fica vazio para o jogo e os arquivos são renomeados para `.corrompido`,
  nunca apagados.

**Queda de energia em cada ponto:** antes do passo 3, o destino antigo está intacto. Durante o passo
3, o NTFS garante a renomeação atômica e o `.bak` cobre a janela do `ReplaceFileW`. Depois do passo 3,
vale o novo.

### Organização dos seis slots

- **Pasta:** `userDir()\saves\card0\`. É a mesma regra do `controles.toml`: ao lado do executável se
  a pasta for gravável, senão `%LOCALAPPDATA%\BlueStingerRecomp`. Na reinstalação pelo Builder, os
  arquivos do usuário que estão na instalação vão para a nova (`builder_core.cpp`, "arquivos do
  usuário... vão junto"). Basta conferir que `saves\` não está na lista do pacote, o que já vale por
  construção.
- **Nomes:**
  - `BLUESTIN.000.bsv` a `BLUESTIN.005.bsv` (os nomes do jogo);
  - `.bak` e `.tmp` ao lado;
  - nenhum outro nome é aceito, nem `..` nem separadores.
- **Envelope `.bsv` v1** (little-endian, sem cópia de `struct`):

  | Campo | Tamanho |
  |---|---|
  | magic `BSSV` | 4 |
  | versão do envelope | `u16` |
  | tamanho do cabeçalho | `u16` |
  | tamanho da imagem (2 048) | `u32` |
  | CRC-32 da imagem | `u32` |
  | data | `i64` |
  | reservado | 8 |
  | imagem VMS inalterada | 2 048 |

  Versão maior que a conhecida: o arquivo é recusado e preservado.
- **Cartão:**
  - a pasta `card0` existe e equivale a um cartão formatado;
  - a pasta ausente equivale a cartão ausente;
  - um `card.bsc` opcional guarda capacidade e "somente leitura" (pendência 6).

### Builder e projeto C++ exportado (verificado sem build)

- O kit (`make_builder.ps1`) e a exportação (`project_export.cpp`, `kKitEntries`) copiam as árvores
  inteiras de `app`, `native` e `transitional`. Os arquivos novos do N4 (`native/platform/saves_*.cpp`,
  `transitional/bu_adapter.*`) entram sem mudar o Builder, desde que entrem no `CMakeLists.txt` de cada
  pasta.
- **Nenhuma dependência nova:**
  - `std::filesystem` (C++20, já usado em `app/main_sdl.cpp`);
  - Win32 `ReplaceFileW`, `MoveFileExW` e `FlushFileBuffers` (`kernel32`, já ligada por padrão);
  - o CRC-32 do envelope é uma função de 20 linhas, sem biblioteca;
  - o TOML (`card.bsc`, se usado) já está no kit.
- `.gitignore`: `*.bin` e `/saves/` (raiz) não pegam fontes. As fixtures do oráculo ficam em
  `analysis/` (ignorada). Os testes usam pasta temporária.

### O que deixa de existir com o provedor nativo (`--saves nativo`)

| Mecanismo legado | No nativo |
|---|---|
| as 14 entradas `bu` | slots do adaptador (as rotinas puras `8C08CA5C`, `8C08CC58` e `8C08CDE2` ficam no recompilado) |
| servidor `bu` `8C08BF50` (registrado pelo init via `8C17BFB4`) | não é registrado, porque o init está no slot; o fechamento exige 0 passadas |
| fila e estado internos (`8C090xxx`, `8C091018`, `8C09945A`/`8C0994B4`/`8C094FF0`) | não chamados |
| comandos Maple de armazenamento e a função de memória do `per` para a VMU | 0 no fechamento (`BS_BU_FECHAMENTO`) |
| `pdLcd` (`8C15E534`, `8C15E5BC`, `8C15E6D8`, `8C15E73C`) | "LCD não suportado", sem Maple |
| callbacks do jogo `8C01C2D2` e `8C01C364` | **continuam**: são código do jogo, chamados pelo adaptador com os `op`/`arg` do oráculo, em vez do servidor |
| eventos de inserção (`op 0x12`) e montagem (`op 1`) | sintetizados pelo adaptador no boot, na ordem que o oráculo c1 mostrar |

### Sequência mínima de captura no Flycast (quando autorizada, depois do N3)

Os comandos estão em "Comandos da captura". Ordem mínima até o primeiro SAVE e LOAD com sucesso, e
quando parar:
1. **c0**, calibração, cerca de 10 min: os passos de título e painel. Se a marca `8C01D200` não
   aparecer em 2 500 passos, ajustar o prefixo do `painel_a.txt` pelos passos das marcas antes de
   seguir.
2. **c1**, LOAD GAME com a VMU vazia: init, montagem, `disk-info`, seis `FF05`. Isso já fecha a ordem
   inserção/montagem do boot.
3. **c2**, SAVE forçado (H1). Se o `forcado` aparecer e não houver `entrada save` em 4 000 passos, H1
   caiu: usar o fallback 3a (modo em `[8C01D244]`, valor do trace) antes do 3b.
4. `n4_vmu.py extrai` e `valida --bluestinger` no `c2.bin`.
5. **c3**, processo novo, LOAD do `BLUESTIN.000`: o despejo do destino `+0x80` deve ser igual ao
   payload do c2.
6. Repetir c1–c3 uma vez (critério 1). Depois, os erros da matriz.

Tempo estimado das capturas: 1–2 h, sem os fallbacks.

### Decisões do mantenedor (2026-10-19, aprovação provisória da arquitetura)

1. **Local:** `Documentos\DC-Recomp\Blue Stinger\saves\card0\`.
   - Resolvido pela pasta conhecida `FOLDERID_Documents` (`SHGetKnownFolderPath`), que respeita o nome
     localizado e o redirecionamento (OneDrive), não pelo caminho literal.
   - **Modo portátil explícito:** `--saves-portatil` usa `saves\` ao lado do executável e
     `--saves-pasta <dir>` usa uma pasta qualquer. Nada depende de a pasta do executável ser gravável.
   - **Substitui a pendência 5** abaixo (`userDir()`).
2. Envelope `.bsv` versionado com a imagem VMS inteira; importação e exportação de VMS puro à parte.
3. `ISaveStore` síncrono e adaptador `bu` assíncrono, separados.
4. **`.bak`:**
   - só restaura com destino ausente ou inválido; um arquivo válido nunca é trocado por backup antigo;
   - toda recuperação e toda quarentena vão para o log;
   - um save de versão futura nunca é sobrescrito nem posto em quarentena.
5. Seis slots, um cartão lógico na unidade 0.
6. Flash e emulação de VMU fora do N4 (N4 = saves).
7. `--saves legado` é o padrão durante o desenvolvimento. **No encerramento do N4, depois da
   validação, os saves nativos viram padrão**, como os controles no N3, e o legado fica como fallback
   temporário.

### Decisões pendentes do mantenedor (as de 1, 3, 4, 5, 6 e 9 foram resolvidas acima)

| # | Decisão | Opções | Vantagens e riscos |
|---|---|---|---|
| 1 | Escopo da flash | N4a (saves) + N4b (flash, `Maple → 0`) / N4 ampliado | N4a entrega save antes; N4 ampliado fecha a meta de uma vez, mas é mais longo |
| 2 | Oráculo | Flycast A1 (recomendado) / análise estática / VMU de desenvolvimento | ver "As duas alternativas" |
| 3 | `.bak` com destino inválido | recuperação automática com log / perguntar ao jogador | automática não trava o jogo; perguntar exige UI fora do jogo original |
| 4 | Formato em disco | **(a)** envelope `.bsv` com o VMS dentro / (b) VMS puro de 2 048 bytes (+ `.vmi`) / (c) imagem de VMU de 128 KB (`.bin`, como Flycast e redream) | (a) versionável e com checksum próprio, mas não abre direto em emulador (um `n4_vmu.py` exporta); (b) interoperável com ferramentas de VMS, mas sem versão nem checksum além do CRC do VMS; (c) troca de saves com emuladores, mas exige manter uma FAT de VMU no produto, o que é estrutura de hardware no `native/` e contraria a arquitetura. **Afeta compatibilidade: decisão do mantenedor** |
| 5 | Local | `userDir()` (como os controles) / sempre `%LOCALAPPDATA%` | o primeiro é portátil e o Builder preserva; o segundo sobrevive a apagar a pasta do jogo |
| 6 | Cartões e capacidade | um cartão na unidade 0, a 1 ausente (recomendado) / dois cartões; capacidade de 200 blocos como uma VMU | dois cartões só se o trace mostrar uso; a capacidade muda as mensagens de "cheio" |
| 7 | CRC VMS inválido no LOAD | seguir a `bu` original (oráculo e_crc) | sem alternativa sensata antes do trace |
| 8 | Importar saves de emulador | nenhuma / comando manual (`n4_vmu.py` → `.bsv`) / automática | manual não arrisca dados; automática exige detectar formatos |
| 9 | Padrão | `--saves legado` até os portões / nativo direto | política do projeto: legado até passar os portões |

## Revisão do Codex das seções acima (`reviews/codex-20261019-012621.md`)

Somente leitura, sem builds nem execução. Todos os achados foram conferidos no `bs-disasm` e aceitos.

| Achado | Decisão |
|---|---|
| [MÉDIA] `8C08C7BE` também só é alcançado por literal morto (`8C01C510`/`8C01C4D2`) e faltava | aceito, conferido: entra no inventário (+1), com um critério único para literal morto |
| [MÉDIA] o passo 4 habilita SAVE/LOAD sem oráculo de sucesso (o trace sem cartão não mostra um) | aceito: o passo 4 ganha um portão (harness da `bu` original ou análise do despacho) |
| [MÉDIA] os unitários não cobrem a coerência de `disk-info` × `exists` × armazenamento | aceito: unitário 9 |
| [BAIXA] a explicação do aliasing por unidade estava aritmeticamente errada | aceito, corrigido |
| [BAIXA] "outro" não fixava os códigos do callback | aceito: SAVE `0x0C`, LOAD `0x0E` (conferidos em `8C08C836`/`8C08C8F2`), chegada ao callback não verificada |
| [BAIXA] durabilidade sem teste em nova abertura do store | aceito: unitário 10 |

Confirmados sem problema: os callbacks, o servidor e o init/exit, o CRC sobre 1 840 bytes com o campo
`0x46` zerado, os offsets VMS e os argumentos do LOAD.

## Contrato que o `ISaveStore` atual não cobre

1. **Protocolo assíncrono:** aceito → ocupado → concluído → último erro, com callback de conclusão
   uma única vez, progresso determinístico e segunda operação recusada enquanto há uma ocupada.
2. **Imagem VMS:** cabeçalho de 128 bytes (descrição curta "BlueStinger", longa "BLUESTINGER SAVE
   DATA", identificador de 16 bytes), um ícone de 512 bytes, payload de 1 200 bytes (`8C24F93C`),
   CRC de 16 bits (0x1021). São 1 840 bytes, arredondados para 4 blocos (2 048). O LOAD lê a partir do
   bloco 1 para `8C24F8BC`, deixando o payload em +0x80, então a geometria VMS faz parte do contrato.
   Proposta: guardar os 2 048 bytes VMS canônicos dentro de um envelope nativo versionado (a versão
   pertence ao envelope, não ao VMS).
3. **Capacidade e erros:** disco ausente (`0xFF01`), não formatado (`0xFF03`), não encontrado
   (`0xFF05`), não abre (`0xFF07`), blocos livres e entradas livres. Gravação atômica: um erro no meio
   não destrói o save anterior.
4. Delete e format pelo jogo: não verificados.

## Ordem de implementação proposta (quando autorizado)

1. Investigação curta: conferir no disassembly as assinaturas da tabela e os callbacks.
2. Contrato: `ISaveStore` estendido (envelope versionado, estado, erros) e unitários de serialização
   e CRC, contra as rotinas puras como oráculo.
3. Adaptador `transitional/bu_adapter` com slots na API pública e o protocolo assíncrono; `pdLcd`
   respondendo "não suportado".
4. Testes rápidos:
   - imagem de 1 200 → 2 048 bytes;
   - offsets e CRC;
   - leitura a partir do bloco 1;
   - recusa de imagem truncada ou de versão futura;
   - disco cheio, somente leitura e arquivo ausente;
   - segunda operação durante "ocupado";
   - callback chamado uma vez só;
   - gravação interrompida preserva o save anterior.
5. Gameplay assistido, em poucas rotas curtas:
   - salvar `BLUESTIN.000`, reiniciar o processo e carregar;
   - comparar sala, inventário e estado;
   - sobrescrever um save;
   - disco cheio, com a mensagem correta.
6. Revisão do Codex e regressão completa no encerramento do N4.

## Revisão arquitetural antecipada (Codex, `reviews/codex-20261018-225209.md`)

**Arquitetura recomendada (incorporada):**
- **`native/` sem ABI do SH-4.** O `ISaveStore` fica só com o domínio persistente: `CardState`
  (presente, formatado, somente leitura, blocos e entradas totais e livres), `SaveImage` (nome, os 2 048
  bytes VMS, data), `load`, `save_atomic`, `list`, `remove` e erros nativos descritivos.
- **`transitional/bu_adapter`** cuida do protocolo assíncrono por unidade (`Idle → Busy → Complete`,
  operação pendente, progresso, último erro, guarda contra segunda operação), dos callbacks guest
  registrados por `8C08C074`/`8C08C07A` (chamados pelo despacho, como no `gdfs_adapter`) e do
  mapeamento de erros (`FF01/03/05/07`). A área nativa do BIOS HLE não é usada: as estruturas vão
  para os buffers que o jogo fornece.
- **Envelope em disco**, little-endian e sem cópia bruta de `struct`: magic, versão, tamanho do
  cabeçalho, tamanho da imagem (2 048), checksum do envelope e a imagem VMS inalterada. O CRC VMS é
  conferido na gravação. Rejeitar CRC inválido no LOAD só depois de provar que a biblioteca original
  faz isso (não verificado).
- **Disco:** `userDir()/saves/card0/`, arquivos `BLUESTIN.00n.bsv`, um `card.bsc`, gravação em
  temporário no mesmo diretório, releitura, troca com `.bak` e só então a conclusão para o jogo.
  Nenhuma migração inicial (não há saves legados).
- **`--saves legado|nativo`**, padrão `legado` (exatamente o atual: sem VMU, LOAD GAME acessível,
  SAVE indisponível) até passarem reinício de processo, sobrescrita, falha atômica e fechamento.
- **Fechamento:**
  - corrigir a classificação de propriedade em `config/fronteiras_us.toml` (os pontos `bu` estão sem
    subsistema; `8C08BDEC` está como tempo);
  - slots para init, os dois callbacks, mount, disk-info, exists, load, save, status, último erro e
    exit;
  - `BS_BU_FECHAMENTO`, análogo ao `BS_PAD_FECHAMENTO`, contando as famílias `bu`, `per` e `pdLcd`
    geradas e os comandos Maple de memória;
  - um negativo que devolva uma operação ao legado tem de falhar a suíte.

**Já verificado na revisão:**
- pilha de `8C08C818` = `time*` e depois a flag `0x80000000`;
- `8C08CA34` = (drive, work, size);
- callbacks `8C01C2D2` (conclusão) e `8C01C364` (progresso) registrados em `8C01C37A`.

**Falta verificar antes de codificar (trace do legado com os callbacks):**
- argumentos e ordem "estado concluído × callback × último erro";
- unidade e frequência do progresso;
- ocupação global ou por unidade;
- códigos de cartão ou diretório cheio e de somente leitura;
- quem valida o CRC no LOAD.

**Incrementos (revisados):**
1. trace do legado;
2. envelope, VMS e armazenamento atômico com unitários, sem instalar no jogo;
3. slots, classificação, fechamento e `--saves nativo` com cartão ausente (o boot legado não muda);
4. init, mount, disk-info, exists e lista (cartão vazio no menu);
5. LOAD de imagem conhecida;
6. SAVE de nome novo, com reinício de processo;
7. sobrescrita atômica, cheio, somente leitura e recuperação;
8. fechamento, gameplay e regressão completa;
9. depois: delete, format, segundo cartão e migração, se o trace mostrar uso.

**Decisões do mantenedor:**
1. **Escopo da flash do sistema.** A arquitetura (N0) atribui ao N4 "VMU, backup **e flash**" e a meta
   `Maple → 0`. Opções: N4a (saves VMS/`bu`) + N4b (flash e fechamento completo), ou ampliar o N4.
   Recomendação: N4a/N4b.
2. **Cartões:** um cartão formatado na unidade 0 e a segunda ausente (recomendado), ou dois cartões.
3. **Recuperação pelo `.bak`:** automática (recomendada, com registro no log) ou com confirmação.
4. **Pasta:** a do `userDir()`, ao lado do executável e, se for somente leitura, em `%LOCALAPPDATA%`
   (recomendada, igual aos controles), ou sempre `%LOCALAPPDATA%`.
5. **CRC VMS inválido no LOAD:** seguir a biblioteca original quando o trace mostrar o que ela faz.
