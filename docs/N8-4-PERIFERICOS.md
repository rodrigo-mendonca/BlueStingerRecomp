# N8.4: controles, saves e Maple

**Situação (2026-10-27): concluído.** Executado pela autorização contínua do mantenedor para o N8.4 em diante (2026-10-26:
"continuar sem esperar aprovação; commit e publicação a cada fechamento"). Referência imutável do N8.3: o commit
`a28e79a`.

## 1. Inventário (somente leitura)

### 1.1 Uso do barramento

**Com os provedores nativos** (padrão desde o N3/N4: pd e bu atendidos pelos adapters):
- o jogo não tem controle nem VMU no barramento;
- a camada per do SDK (código recompilado) continua a sondar as portas a cada VBlank: escreve `MDSTAR` e `MDST` em
  `8C0975C0` (o slot de amostragem do N3) e lê o `MDST` em `8C0975C0`/`8C0976DE`;
- a tabela de inicialização do SDK (`8C08BD74`) e `8C09727C` gravam os registradores uma vez;
- medido (`BS_MAPLE_DEBUG`, `newgame_area1`, 6 000 VBlanks): **5 982 DMAs**. Cada um tem **4 descritores**,
  `DEVICE_INFO` (0x01) ao dispositivo principal de cada porta, sem dados e no padrão 0. Ninguém responde, e o buffer
  recebe `0xFFFFFFFF`. Nenhum `GET_CONDITION`.

**Efeito observável** (parte das referências: ordem de interrupções e trace H/A/C):
- o fim do DMA é agendado a 100 ticks por bit;
- ele acende o `ISTNRM` bit 12;
- o `MDST` lê 1 até o fim;
- o primeiro acesso ao `MDST` depois do VBlank-In avança a transição dos roteiros.

### 1.2 O que ficava no produto

| Item | Uso no produto |
|---|---|
| `runtime/maple` | modelo do barramento **com o controle da porta A** (DEVICE_INFO, GET_CONDITION, ALL_STATUS, RESET), diagnósticos `BS_MAPLE_PORTS`, `BS_MAPLE_FUNCAO`, `BS_MAPLE_DEBUG`, `BS_PAD_MUTACAO`, `BS_PAD_LEITURAS`; no nativo, sem controle |
| `input_bridge::install` | fonte do Maple tirada do `InputSystem` (`--controles legado` e o negativo `maple`) |
| `--controles legado` | controle no Maple pela ponte |
| `--saves legado` | a bu original sobre o barramento sem VMU ("VMU not found") |

**O que fica por ser nativo ou do substrato:**
- os adapters `pad_adapter`/`bu_adapter` (ABI do guest, L15/L16, até o N10);
- o `InputSystem` e os roteiros (`runtime/input`: `BS_INPUT_SCRIPT`, gravações);
- as conversões e o gravador de sessão do `input_bridge`.

## 2. Opções e decisão

| Opção | Resumo | Decisão |
|---|---|---|
| **A** | serviço nativo do barramento sem dispositivos, com o contrato medido (como os serviços do N8.1/N8.3); o modelo do Maple só na validação | **escolhida** (recomendada; autorização contínua) |
| B | slot em `8C0975C0` sem disparar o DMA | muda a ordem das interrupções e o `MDST`: quebra as referências |
| C | manter o modelo do Maple com o controle ausente no produto | o modelo do dispositivo continuaria alcançável no produto |

## 3. Arquitetura

- **`system/maple_bus`** (papel `nativo`):
  - os registradores `0x005F6C00-0x005F6CFF` guardam o valor escrito;
  - `MDST = 1` percorre a tabela: grava `0xFFFFFFFF` em cada buffer de recepção, agenda o fim no mesmo instante do
    modelo e conta os DMAs e os `DEVICE_INFO`;
  - o `MDST` lê 1 até o fim;
  - fora do contrato é erro explícito (`sh4_fatal`, contado em `maple_bus_refused`): outro comando ou destino, quadro
    com dados, padrão, disparo por VBlank (`MDTSEL`). Pediria um dispositivo ou um modo que não existe no produto;
- **fachada `periph_*`:** o system block, o `holly_intc`, o `GameHost` e o adapter do pd usam o provedor ativo. O
  padrão é o serviço nativo; a validação pode instalar o modelo (`maple_bus_backend`);
- **modelo do Maple** (`runtime/maple`): passa para o `bs_runtime_hle` (validação e ferramentas). O `GameHost` o
  instala com `--controles legado`, `--saves legado` e o negativo `BS_PAD_NATIVO_MUTACAO=maple`;
- **produto:** recusa (saída 2) `--controles legado`, `--saves legado`, `BS_PAD_NATIVO_MUTACAO=maple` e os
  diagnósticos do modelo (`BS_MAPLE_PORTS`, `BS_MAPLE_FUNCAO`, `BS_MAPLE_DEBUG`, `BS_PAD_MUTACAO`, `BS_PAD_LEITURAS`);
- **`input_bridge::install`:** só na validação;
- **`ControlsProvider::None` e `SavesProvider::None`:** nenhum adaptador e o barramento sem dispositivos, só para os
  jogos falsos dos testes (a linha de comando não os oferece).

### 3.1 Suítes no exe de validação

O código nativo no exe de validação é o mesmo do produto. Passam a usá-lo:
- as suítes que comparam o legado com o nativo ou rodam o cenário histórico "sem cartão" (`--saves legado`):
  `test_n2b2`, `test_n8_nativo`, `test_pd_nativo` e `test_controles`;
- o caso S5 do `test_saves`.

As 16 referências, `test_assistido`, `test_preview`, H9 e `test_builder`/`test_projeto` continuam no produto.

### 3.2 Soft reset: o menu do sistema

**O que acontece:**
- o soft reset (A+B+X+Y segurados e START) chama o stub `8C15E9C4`: `mov #1,r4` e um salto pelo vetor `8C0000E0`
  (MISC; no KallistiOS, o seletor 1 é o menu do BIOS, sem retorno);
- antes do N8.3, o HLE parava com "syscall do BIOS ainda não implementada";
- no N8.3, o serviço nativo recusava com outra mensagem. As referências não fazem soft reset, e o `test_pd_nativo` acusou
  isso (9 falhas na primeira rodada).

**Agora:**
- `MISC r4 = 1` é o pedido do menu do sistema: o serviço registra "o jogo pediu o menu do sistema", conta
  (`menuExit`) e chama o gancho do host;
- o `GameHost` encerra a sessão sem erro (`StopRequest`), como o console, que sai do jogo para o menu;
- sem gancho, ou com outro r4 (o `8C15E9D6` usa r4 = 2), continua erro explícito;
- o modelo HLE da validação não mudou.

A detecção do `test_pd_nativo` e do `pad_traco.py` trocou a mensagem do HLE pela nativa e passou a exigir saída 0. As
conferências semânticas ficaram iguais:
- A+B+X+Y segurados no último passo (40 a 43);
- a mensagem só no caso com START;
- o controle sem START completo e sem a syscall;
- traço e quadros iguais entre o legado e o nativo.

## 4. Revisões independentes (Codex)

**`codex-20261026-184512`:** 3 achados, aceitos e corrigidos:

| Achado | Correção |
|---|---|
| [MÉDIA] o teste não provava o instante exato do fim do DMA | `MDST`/`ISTNRM` conferidos em 0, 12 800, 25 599, 25 600, 25 601 e 40 000 ticks, nos 3 DMAs e nos dois provedores |
| [BAIXA] `BS_MAPLE_DEBUG` vazio passava no produto | recusado pela presença, como o modelo o liga |
| [MÉDIA] o build `mods` sem configuração reproduzível; `test_controles` passava sem o exe | presets `msvc-release-mods` e `msvc-release-watch`; `tools/dist/valida_exe.ps1` reprova exe ausente ou do produto nas 10 suítes do legado (prova offline: produto rc 2, validação aceita, ausente rc 2) |

**`codex-20261026-230114`** (segunda revisão, o soft reset, o reset entre sessões e o restante): nenhum achado
funcional. Ela conferiu os bytes de `8C15E9C4`, a segurança do `StopRequest` dentro da syscall e que os critérios do
`test_pd_nativo` não enfraqueceram.

## 5. Equivalência e testes (2026-10-27)

**Exe final do produto:** as 16 referências dão 16/16, com as mesmas 179 exceções de SPC e nenhuma nova, nos dois
modos de tempo.

| Teste | Resultado |
|---|---|
| unitários | 327/327 no produto, 329/329 na validação (novos: equivalência com o modelo tick a tick, 7 recusas, transição, reset entre sessões com DMA pendente, menu do sistema) |
| ctest Debug | 19/19, com e sem o disco |
| catraca `n8_produto_papeis` | produto 35 objetos (sem `maple.cpp`, com `maple_bus.cpp`), validação 42; recusas de `--controles legado`, `--saves legado`, `BS_PAD_NATIVO_MUTACAO=maple` e dos diagnósticos |
| `test_pd_nativo` (validação) | 86 casos, 0 falhas: as 9 do soft reset sumiram |
| `test_saves` | 25/25: SAVE/LOAD, importação e exportação de VMS, cartão temporário; o S5 legado no exe de validação |
| `test_n8_nativo` (validação) | 26/26 |
| `test_n2b2` (validação) | 17/17 (N12: experimental 17,96 ms × estável 21,58 ms) |
| `test_assistido` | 28/28 (nativo × experimental) |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 |
| `test_preview` | 12/12 |
| H9 na janela | 0 faltas, 0 erros, 0 perdas |

**Reset entre sessões:** o boot (`holly_sb_reset`) descarta o DMA pendente, sem interrupção tardia. Zera os
registradores, os contadores e a transição, igual nos dois provedores (unitário). A sessão volta ao provedor nativo no
fim (`periph_use(nullptr)`).

**Saves:** são os mesmos arquivos nativos do N4 (envelope `.bsv`). Importar e exportar VMS dá o mesmo resultado, byte a
byte. Os saves do usuário não foram tocados.

**Tamanhos:**

| Exe | N8.3 | N8.4 |
|---|---|---|
| produto | 19 247 104 bytes | 19 244 544 bytes |
| validação | — | 19 286 016 bytes |

**Hashes:**
- produto `6AA18D7D…`;
- validação `B72740E7…`;
- Preview `CE4A02EB…`.

**Rollback:**
- a Preview do N8.3 (`5D8BD6FE…`) em `dist\BlueStinger-Preview.anterior`;
- a do N8.2 em `.n8-2-cc370d56`;
- a do N8.1 em `.n8-1-9ecdeb7`.

O `controles.toml` e as legendas foram preservados. O pacote do Builder tem 44 MB.
