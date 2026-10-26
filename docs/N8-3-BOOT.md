# N8.3: boot, BIOS, flash e GD-ROM

**Situação (2026-10-26): concluído.** Autorizado pelo mantenedor, só o N8.3. Referência imutável do N8.2: o commit `3984dd5`.

Decisões do mantenedor (2026-10-26, depois do inventário):
- **opção A:** serviço nativo na própria fronteira das syscalls, no padrão do serviço do TMU do N8.1:
  - sem regenerar e com o AOT e a RAM guest intactos;
  - fora do contrato é erro explícito;
  - o HLE fica só na validação e nas ferramentas;
- **flash volátil:** a partição 2 começa em branco (0xFF) a cada sessão, só em memória, como nas 16 referências;
- **HLE na validação e nas ferramentas:** o executável de validação mantém o `--arquivos legado`, o `--flash` e o tempo
  reios para as suítes históricas. O Builder, o `bs-diff` e os testes com o CDI continuam no modelo.

## 1. Inventário (somente leitura)

### 1.1 Boot

O boot nativo (`runtime/boot/native_boot.cpp`, desde o N0) já não executava BIOS nem IP.BIN:
- copia o `1ST_READ.BIN` desembaralhado para `0x8C010000` e começa nele (`0xAC010000`);
- monta a área do sistema `0x8C000000-0x8C007FFF` medida no oráculo:
  - `0xFF`;
  - as variáveis, os vetores e os stubs das syscalls;
  - a tabela `0x8C003174` (`bios_entry_state.inc`);
- carrega os registradores do PVR/Holly deixados pelo BIOS.

Essa imagem é a RAM inicial que as referências comparam: fica como dado do boot. Os registradores do PVR pertencem ao corte
gráfico (N8.7) e não foram tocados.

### 1.2 O que o jogo usa: só as syscalls

**Hardware do drive:**
- o jogo nunca toca os registradores do GD-ROM (`0x005F7000-0x005F70FF`), a ROM do BIOS nem a flash
  (sonda `BS_HW_PROBE` no executável de validação, `newgame_area1`, 6 000 VBlanks);
- as 16 referências do N8.0 não têm nenhum acesso a hardware não mapeado;
- o G1 (`0x005F7400-0x005F74FF`) recebe uma escrita só: `GDST = 0` (DMA do GD parado), da tabela de inicialização de
  hardware do SDK (`8C08BD74`, tabela `8C0B8FB4`), e nunca é lido.

**Portas de syscall:** toda a dependência passa por 21 stubs de syscall do SDK (`mov.l @vetor,r0; jmp @r0`):

| Família | Stubs | Uso medido (provedor nativo de arquivos, `BS_HLE_LOG`, 6 000 VBlanks) |
|---|---|---|
| GDROM (`0x8C0000BC`) | 13: `8C094DEC`-`8C094EEC` (r7 0-10 e 2 com r6 = -1) | INIT_SYSTEM (`8C17AD24`), GET_DRV_STAT (17×), REQ_CMD, GET_CMD_STAT e EXEC_SERVER (cerca de 73 mil: driver gdc `8C17B022` e a passada do servidor do CRI `8C1586B8`). Comandos: INIT (`0x18`), GET_VERSION (`0x28`) e 2 leituras por DMA de 1 setor no boot (LBA 239371 e 239378, metadados do volume lidos pelo gdFs, para `0C296220`) |
| FLASHROM (`0x8C0000B8`) | 4: `8C09A208`+ | só a partição 2: INFO; READ de `0x1C000` (0x40 bytes); DELETE de `0x1C000`; WRITE de 0x12 bytes em `0x1C000`; READ de `0x1C000` e de `0x1FFC0` (biblioteca da flash `8C1993F2`-`8C1994F8`) |
| SYSINFO (`0x8C0000B0`) | 3: `8C099510`+ | só ID (r7 = 3, `8C094F48`), que devolve `0x8C000068` |
| MISC (`0x8C0000E0`) | 1: `8C15E9D6` | não usado (já era fatal no modelo) |
| ROMFONT, GDROM2 | nenhum literal no binário | — |

**Arquivos:** todos vêm do provedor nativo do N1 (origem `nativo` no registro de arquivos). As 2 leituras de setor do boot
aparecem com a origem `gdrom` (tipo `outro`).

### 1.3 O que ainda ficava no produto

| Item | Antes do N8.3 |
|---|---|
| `hle/bios.cpp`, `hle/flashrom.cpp` | BIOS HLE completo (6 vetores) e flash de 128 KB com 5 partições |
| `gdrom/gdrom.cpp`, `gdrom/data_source.cpp` | modelo do drive: fila de pedidos, TOC, PIO e DMA, sessões, perfil de tempo reios e setores por FAD |
| `--arquivos legado` | gdFs original lendo setores pelo modelo do drive |
| `--flash <arquivo>` | imagem de flash de 128 KB |
| `BS_GDROM_TIMING=reios` | perfil de tempo do oráculo |

## 2. Opções avaliadas

| Opção | Resumo | Decisão |
|---|---|---|
| **A** | serviço nativo com contrato estreito nos endereços dos vetores (como o serviço do TMU do N8.1) | **escolhida** |
| B | slots `[native]` nos 21 stubs | regenera o `generated/`: muda o AOT e os ciclos do modo estável |
| C | slots na API do SDK (driver gdc e biblioteca da flash) | reimplementação maior, com o estado interno do guest na RAM diferente |

## 3. Arquitetura

**Antes:** os vetores apontavam para o BIOS HLE (`0x8C001000`+, os mesmos endereços do reios), que simulava BIOS, drive e
flash, no produto.

**Agora:**
- **`system/syscall_service` (papel `nativo`):** atende os mesmos endereços, com o mesmo efeito em r0, na RAM e no hash
  do estado, só no contrato medido:
  - **GDROM** (r6 = 0): INIT_SYSTEM, GET_DRV_STAT (pausa, GD-ROM), REQ_CMD, GET_CMD_STAT e EXEC_SERVER, com os
    comandos INIT, GET_VERSION e leitura por DMA. A leitura termina inteira no EXEC_SERVER, que é o perfil instantâneo
    do modelo, padrão do produto. Os setores vêm do `RuntimeDisc` nativo (`bs_disc_data`): não há disco, FAD de faixa,
    TOC nem fonte de setores;
  - **FLASHROM:** só a partição 2 (`0x1C000`, 16 KB), em memória, com INFO, READ, WRITE (AND) e DELETE. Fica volátil:
    em branco a cada boot;
  - **SYSINFO:** só ID;
  - **tudo o mais é componente removido:** outra função, comando, superfunção, partição, faixa ou vetor (ROMFONT, GDROM2,
    MISC). Vira erro explícito (`sh4_fatal`, contado em `syscall_service_counts().refused`), sem recuo silencioso;
  - **setor ilegível:** os dados foram conferidos na abertura, então é instalação incompleta. Erro claro, nunca setor em
    branco;
  - **registro de arquivos:** o mesmo do modelo, origem `gdrom`, para o comparador das referências não mudar;
- **fachada `sys_disc_*`:** o poll central (`sh4_service`), o detector de ociosidade da thread 3 (`cri_idle`) e o
  diagnóstico do CRI leem o estado pelo provedor ativo. O nativo dá o mesmo hash do modelo no perfil instantâneo e 0
  blocos de tempo;
- **modelo HLE (`bs_rt_hle` → `bs_runtime_hle`):** `bios`, `flashrom`, `gdrom` e `data_source`, fora do `bs_runtime`:
  - ligado pela validação (`bs_app_core` com `BS_VALIDACAO`, papel `hle` no manifesto) e pelas ferramentas
    (`bs_runtime_cdi`: Builder, `bs-diff` e testes com o CDI);
  - na validação, `--sistema hle` o escolhe. Ele fica implícito com `--arquivos legado`, `--flash` e `BS_GDROM_TIMING`.
    O `bs-diff` usa sempre o modelo, porque compara com o oráculo;
- **produto:** o `--sistema hle` e o que o implica são recusados com saída 2 e mensagem ("so existe no build de
  validacao"). O `GameHost` do produto não tem o caminho HLE;
- **registradores do drive e do G1** (`holly_sb`, nos dois builds, porque nenhum deles modela o drive por registradores):
  - qualquer acesso a `0x005F7000-0x005F70FF` é erro explícito;
  - no G1 só a escrita `GDST = 0` da tabela do SDK é aceita, como registrador inerte. O resto é erro explícito;
  - a ROM do BIOS (área 0) continua só registrada como acesso não mapeado, para não transformar um ponteiro nulo do jogo
    em queda;
- **constantes da área do sistema** (vetores, endereços das syscalls, sysinfo e área nativa do N3-2):
  `system/system_area.h`, sem dependência do HLE.

### 3.1 Contratos que mudaram

| Contrato | Antes | Agora |
|---|---|---|
| boot | `boot_prepare` instalava o BIOS HLE e zerava o modelo do drive | instala o serviço nativo e zera os pedidos e a flash (volátil) |
| dados para as leituras de setor | `gdrom_set_disc(open_runtime_data(...))` | `syscall_service_set_data(RuntimeDisc)` (sem mods: valem pelo provedor nativo de arquivos) |
| flash | 128 KB, persistente no processo, `--flash` | partição 2 volátil por sessão; `--flash` só na validação |
| `--arquivos legado`, `BS_GDROM_TIMING` | produto | só validação (o produto recusa) |
| consumidores do estado do drive | `gdrom_*` | `sys_disc_*` (provedor ativo) |

### 3.2 Catraca

**`n8_produto_papeis` (`tests/n8/check_papeis.cmake`):**
- o mapa do produto não tem `bios`, `flashrom`, `gdrom` nem `data_source`, e tem o `syscall_service`;
- a validação tem o modelo (papel `hle`);
- o produto recusa `--arquivos legado`, `--flash`, `--sistema hle` e `BS_GDROM_TIMING` (saída 2 com a mensagem), e a
  validação aceita;
- o manifesto completo continua conferido: 35 objetos no produto e 41 na validação.

**Unitários `test_n83_sistema`:**
- o ciclo do boot e a equivalência com o modelo passo a passo (r0, RAM e hash);
- a flash volátil igual ao modelo;
- 16 recusas explícitas;
- dados faltando;
- o registro de arquivos;
- os registradores do drive e do G1.

## 4. O que fica (com justificativa)

| Item | Por quê |
|---|---|
| imagem da área do sistema (`kBiosSysWords`): vetores e stubs | RAM inicial do jogo, comparada pelas referências. É dado, não simulação |
| estado do PVR/Holly na entrada (`kBiosPvrState`) | corte gráfico N8.7 (não antecipar) |
| `sh4_register_hle` | mecanismo genérico de função nativa num endereço do guest (serviço nativo, mods); não é o HLE |
| `GDST = 0` aceito no G1 | escrita da tabela de inicialização do SDK, código recompilado (o AOT não muda) |
| modelo HLE | validação (suítes históricas, `--arquivos legado`, oráculo reios) e ferramentas |

## 5. Revisão independente (Codex, `codex-20261026-142421`)

| Achado | Decisão |
|---|---|
| [MÉDIA] a flash do modelo HLE não voltava ao branco entre sessões no mesmo processo | **aceito**: `flashrom_reset()` no início de cada sessão HLE, antes do `--flash`; teste integrado `game_host_system_provider_per_session` (nativo e duas sessões HLE seguidas), com o negativo provado (sem o reset, o teste reprova) |
| [BAIXA] a catraca aceitava qualquer manifesto com 30 ou mais objetos | **aceito**: contagem exata (35 no produto, 41 na validação) |
| [BAIXA] a aceitação do HLE na validação só era testada com `--version` | **aceito**: o mesmo teste integrado passa pela seleção do provedor no `GameHost` (e, no produto, confere a recusa) |

O Codex conferiu sem problema:
- a máquina de estados e o hash contra o modelo;
- a leitura por DMA e o registro de arquivos;
- as recusas, a fachada e a seleção no produto;
- o `bs-diff` e a guarda do GD-ROM e do G1. Os bytes da tabela do SDK em `0x8C0B921C` são `{0xA05F7418, 0}`.

## 6. Equivalência e testes (2026-10-26)

**Referências:**
- **produto** (`msvc-release`): as 16 referências do N8.0 dão 16/16, com as mesmas 179 exceções de SPC aprovadas e
  nenhuma nova;
- **validação com o modelo HLE** (`msvc-validacao`, `BS_GDROM_TIMING=instant`): 16/16, com as mesmas 179;
- o exe final do produto difere do que rodou as referências só em 4 bytes, os carimbos de data do link (cabeçalho PE
  e diretório de debug). As correções do Codex mexem só em código de `#if BS_VALIDACAO`.

| Teste | Resultado |
|---|---|
| unitários | 323/323 nas duas configurações (9 novos do N8.3) |
| ctest Debug (validação) | 19/19, com e sem disco (inclui a catraca) |
| catraca `n8_produto_papeis` | produto 35 objetos sem o HLE; validação 41 com o papel `hle`; negativo cruzado reprova |
| `test_n8_nativo` | 26/26 |
| `test_n2b2` | 17/17: N6 com o legado de arquivos no exe de validação; N12 experimental 16,01 ms × estável 18,67 ms |
| `test_assistido` | 28/28 |
| `test_arquivos` (validação, legado × nativo) | 6/6 |
| `test_tempo` (validação) | 35/35 |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 (o exe exportado recusa `--arquivos legado`) |
| `test_preview` | 12/12 |
| H9 na janela (Preview) | 0 faltas, 0 erros, 0 perdas |

**Tamanhos:**

| Exe | N8.2 | N8.3 |
|---|---|---|
| produto | 19 248 128 bytes | 19 247 104 bytes: os 4 objetos do HLE saíram e o serviço nativo entrou |
| validação | 19 274 240 bytes | 19 282 432 bytes |

O pacote do Builder tem 44 MB, com o kit.

**Dependências do produto:** sem `bios`, `flashrom`, `gdrom` nem `data_source` (o mapa do linker), e sem o leitor de CDI
(como no N8.2). O `bs_runtime_hle` só é ligado pela validação e pelas ferramentas.

**Hashes:**
- produto `3B3BCA39…`;
- validação `A0C488DF…`;
- Preview `5D8BD6FE…`. A Preview do N8.2 (`CC370D56…`) fica em `dist\BlueStinger-Preview.anterior` para o rollback,
  e a do N8.1 em `dist\BlueStinger-Preview.n8-1-9ecdeb7`.

O `controles.toml` e as legendas EN/PT-BR da Preview ficaram preservados.
