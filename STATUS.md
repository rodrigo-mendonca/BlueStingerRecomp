# STATUS

Cada item registra **o que** foi verificado, **como** e **quando**.

## Marcos

| Marco | Estado |
|---|---|
| M0 esqueleto, README, LEGAL, ARCHITECTURE | concluído (decisões D1–D6 registradas em 2026-10-13) |
| M1 extrator + hash | concluído (commit 4fc7336) |
| M2 decodificador SH-4 + testes | concluído, aguardando revisão |
| M3 descoberta de funções + C++ que compila | concluído (commit 9e0a05f) |
| M4 boot até o primeiro frame | em andamento: oráculo Flycast + trace diferencial; boot recompilado igual ao Flycast nas primeiras chamadas (ver abaixo) |
| M5 PowerVR2 completo | — |
| M6 áudio | — |
| M7 controle e saves | — |
| M8 zerável | — |
| M9 melhorias | — |

## Versão suportada

| ID | Produto | 1ST_READ.BIN | SHA-256 |
|---|---|---|---|
| bluestinger_us_v1000 | T13001N V1.000 (NTSC-U) | 1 975 556 bytes, não embaralhado | `013301f3bd5e8305058f661ec3a0cddcb8468b524a2c5f4d8f88e4d523d21a50` |

Esse hash vem de um rip CDI. Se o rip alterou o executável, o hash do GD
original vai ser diferente. Vamos saber quando houver suporte a GDI.

## Verificações

| Data | Item | Como foi verificado |
|---|---|---|
| 2026-10-13 | CDI v3.5: 2 sessões. Faixa 1 = LBA 0, +227 955 setores; faixa 2 = LBA 239 355, +120 464. Ambas Mode 2, setor de 2336 bytes | `bs-extract --info` e um parser Python independente chegaram ao mesmo layout |
| 2026-10-13 | IP.BIN: T13001N V1.000 1999-07-05, JUE, Katana (não WinCE) | `bs-extract --info`; teste `real_disc` |
| 2026-10-13 | SHA-256 do 1ST_READ.BIN | O C++ (`bs-extract`) e o Python `hashlib` (leitura independente por offset) produziram o mesmo valor |
| 2026-10-13 | Implementação SHA-256 / CRC32 | Vetores FIPS 180-2 ("", "abc", 448 bits, 1M×'a') e valor de verificação do CRC32 `CBF43926` |
| 2026-10-13 | Leitura de ISO9660, incluindo arquivos da sessão 1 (FMV) | Teste `real_disc`: `ENDING.SFD` resolve para a sessão 1, cabeçalho "Sofdec Stream" e pack MPEG em +0x2000 |
| 2026-10-13 | Extração completa: 2092 arquivos, 742 898 028 bytes, em cerca de 11 s (build debug) | Python: `manifest.json` válido. Tamanhos em disco = soma do manifest. SHA-256 de amostras confere |
| 2026-10-13 | Nomes de arquivo maliciosos (`..\`) são rejeitados | Teste unitário `iso9660_rejects_path_traversal_names` |
| 2026-10-13 | Desembaralhamento MIL-CD | **Só ida e volta** (scramble → descramble) em testes. Não foi testado com executável embaralhado real |
| 2026-10-13 | `game-data/` e `ISO/` ignorados pelo git | `git status` / `git check-ignore` |
| 2026-10-13 | Decodificador SH-4: 209 instruções, sem padrões sobrepostos, cada uma alcançada por exatamente 2^(bits livres) opcodes | Testes `decoder_table_has_no_overlapping_patterns` e `decoder_every_op_reachable_with_expected_count` |
| 2026-10-13 | Decodificação e disassembly dos **65 536 opcodes** iguais ao Capstone 6.0.0, exceto divergências revisadas: `fmov.s`≈`fmov` (nome); 32 regras de instruções SH-2A/SH-4A que o Capstone aceita e o SH7091 não tem; `fipr`/`fcnvsd`/`fcnvds`, que o Capstone não decodifica | `tools/dev/compare_capstone.py` + `tools/dev/capstone_review.txt` (regras com regex de operandos, para não esconder erro real) |
| 2026-10-13 | 76 vetores de disassembly, campos (n/m de 4, 3 e 2 bits; imediatos com e sem sinal; deslocamentos escalados), alvos PC-relativos (incluindo `PC & ~3` do `mov.l`/`mova`) e flags (delay slot, condicional, chamada, indireto, ilegal em slot, privilegiado) | Teste `decoder_disassembly_vectors` e `decoder_fields_and_flags`. Os vetores foram conferidos com o Capstone |
| 2026-10-13 | Código real: o entry point `0x8C010000` decodifica como o startup Katana (CCR `0xFF00001C` com ICI e `jmp` para `0x8C0100A0`). Na varredura linear, 32% das palavras são inválidas, porque o arquivo também tem seções de dados. A varredura linear não separa código de dados (isso é trabalho do M3) | `bs-disasm` e `bs-disasm --stats` sobre `game-data/1ST_READ.BIN` |

| 2026-10-13 | Os 265 `BSDATA/R*.BIN` não são overlays de código | Nenhum epílogo `lds.l @r15+,pr`+`rts` (1ST_READ: ~1/KB); todos com o mesmo cabeçalho de 9 offsets (0x24) |
| 2026-10-13 | Semântica de **todas** as 209 instruções existe (tabela Op→função verificada em compilação) | `static_assert` em `tests/unit/test_ops.cpp` |
| 2026-10-13 | DIV1 correto (sem sinal 32/16 e 64/32, com sinal 32/32) | Rotinas de divisão do manual SH-4 executadas instrução a instrução contra a divisão do C++ em 11 000 entradas aleatórias |
| 2026-10-13 | ADDC/SUBC/NEGC (cadeias de 64 bits), ADDV/SUBV, MAC.L/MAC.W com saturação, SHAD/SHLD (incluindo deslocamento -32), `@Rm+`/`@-Rn` com n == m, bancos via LDC SR, FPU PR/SZ/FR, FMOV 64 bits com XD, FTRC saturado/NaN, FIPR, FTRV, FSCA, arredondamento RM=01 | `tests/unit/test_ops.cpp` |
| 2026-10-13 | Análise do executável: 3 883 funções (1 549 por chamada, ~2 290 por ponteiro em dados, ~45 por constante do pool), 430 160 instruções únicas, 13 tabelas de salto (todas com limite achado no código, inclusive via constante do pool, depois da revisão Codex), 0 fins mortos, 0 órfãs, em ~1 s | `bs-recomp --analyze-only` (`analysis/report.txt`) |
| 2026-10-13 | Cobertura: ~200 prólogos `sts.l pr` ainda não alcançados (de ~3 000). Prováveis código morto ou ponteiros calculados; o despacho vai registrar em tempo de execução | `analysis/coverage.bin` + script de lacunas |
| 2026-10-13 | C++ gerado: 3 883 funções, 938 k linhas, 11 arquivos; **compila e linka** (MSVC, debug) | `bs-recomp` + `cmake --build`; `bs-gen-check` valida a tabela de despacho (P0/P1/P2) |
| 2026-10-13 | Fluxo gerado executa corretamente: laço com `bf/s`, slot de `bsr` antes da chamada, switch `braf`, `jsr` via pool, `rts`, preservação de r8/r15/PR | `codegen_exec_0/1`: programa SH-4 sintético → análise → C++ → compila → executa (15 combinações × pool inline/RAM) |
| 2026-10-13 | Nenhuma instrução PC-relativa (`mova`, `mov.w/mov.l @(disp,PC)`) em delay slot no código alcançado do jogo, então a regra especial do manual não se aplica | Varredura das 430 k instruções alcançadas (`analysis/coverage.bin` + disassembly) |

### M4: testes diferenciais contra o Flycast

Método: `flycast_oracle.py trace` grava cada entrada de função conhecida no
Flycast (interpretador, reios, GDB); `bs-diff trace` grava o mesmo a partir do
snapshot de entrada capturado no Flycast (`flycast_oracle.py snap`);
`first-diff --accept tests/diff/boot_accepted.tsv` aponta a primeira divergência,
comparando todas as colunas (r0-r15, PR, SR, FPSCR, FPUL); só as diferenças
listadas individualmente nesse arquivo são aceitas.

| Data | Item | Como foi verificado |
|---|---|---|
| 2026-10-13 | Oráculo abria Flycast em cascata (centenas de instâncias, travou a máquina): `connect()` chamava a si mesma depois de um `replace` global | Corrigido; `finish()` sempre encerra o Flycast que o script abriu. Testado com vigia que mata tudo se houver mais de uma instância: nunca disparou |
| 2026-10-13 | Breakpoint em `0x8C010000` nunca disparava: o Flycast grava `trapa` na RAM e o `IP.BIN` desembaralha o `1ST_READ.BIN` em `0xAC010000` depois do boot (rotina `8C00E35E`), sobrescrevendo o trap | RAM em `0x8C010000` comparada com o arquivo em pausas sucessivas; disassembly do `IP.BIN`. O oráculo para no `jmp @r0` de `8C00E202` e só então arma breakpoints |
| 2026-10-13 | O stub GDB do Flycast descarta o aviso de parada quando ainda está enviando outro pacote (`Connection::send`, "Message dropped") | Breakpoint 3 instruções à frente: CPU parada em `AC008306` (lido via `^C`), resposta nunca chegou. `Gdb.cont()` interrompe e confere o PC quando não há resposta |
| 2026-10-13 | O agente do Flycast só tira o breakpoint do PC se o endereço for idêntico (o boot roda em `AC01xxxx`, o breakpoint é `8C01xxxx`) | Trace preso em `8C010000`; `Gdb.step()` tira e repõe pelo endereço gravado |
| 2026-10-13 | `step` do Flycast custa ~40 ms (cria e encerra a thread de emulação) | 100 passos medidos; trace passou a usar breakpoints nas 3 892 entradas + `cont` (~0,2 s por chamada) |
| 2026-10-13 | `generated/` estava desatualizado em relação ao gerador (sem `BS_FN_ENTER`) | Regenerado; trace recompilado passou de 0 para 35 chamadas registradas |
| 2026-10-13 | `PDTRA` (porta A, `0xFF800030`): bits 8-9 = cabo de vídeo; Flycast responde composto (3) | Divergência na chamada #24 (`8C158E78`, r4 = 3 × 0). Modelo em `runtime/memory/sh4_port.cpp`; teste `port_pdtra_*` |
| 2026-10-13 | Salto para o espelho P2 do próprio código (`mova L,r0; mov.l #0xA0000000,r1; or r0,r1; jmp @r1`) antes de mexer no CCR | Fatal em `8C17BE0E` → `AC17BE18`. `constBefore` resolve `or` de duas constantes; 9 funções novas (3 892), despachos em tempo de execução 2 465 → 2 458 |
| 2026-10-13 | `CCR` (`0xFF00001C`) lido de volta, com ICI/OCI lendo 0; arrays de endereço do cache (`0xF4xxxxxx`) aceitos sem efeito | Divergência na chamada #36 (r0 = 0x10D × 8). `runtime/memory/sh4_ccn.cpp`; teste `ccn_ccr_*` |
| 2026-10-13 | `SPG_STATUS` (`0x005F810C`): vsync derivado do contador de ciclos (campo NTSC 59,94 Hz a 200 MHz) | Recompilado preso em `8C172620` esperando o bit 13. `runtime/pvr/pvr_regs.cpp`; teste `pvr_spg_status_*`. A fase (número da linha) não coincide com o Flycast: só afeta `r2`/`r3` na entrada de `8C172634` |
| 2026-10-13 | `ISTNRM` (`0x005F6900`): VBlank-In/Out acendem ao cruzar as linhas de `SPG_VBLANK_INT`; escrever 1 limpa | Recompilado preso em `8C172804`. `runtime/pvr/holly_intc.cpp`; teste `holly_istnrm_*` |
| 2026-10-13 | `ID`/`REVISION` do PVR (`0x17FD11DB`/`0x11`) e `SB_SBREV` (`0x0B`), valores lidos no Flycast | Divergências na chamada #92 (`8C1C59A4`). Testes `pvr_id_*` e `holly_sb_*` |
| 2026-10-13 | **Boot recompilado igual ao Flycast nas primeiras 3 000 chamadas de função** (registradores r4-r15, PR, SR, FPSCR, FPUL) | `first-diff --accept` (estrito): idênticos; diferenças aceitas: 2 de PR no espelho P2 e 2 de fase do `SPG_STATUS` (r2/r3 em `8C172634`). Trace do Flycast: 9 min 17 s |
| 2026-10-13 | Memcpy desenrolado (estilo Duff) com base da tabela no pool (`8C08D1D0`: `mov.l #tab,r3; mov.l @(r0,r3),r3; add #-4,r0; jmp @r3`) | Fatal em `8C08D1D8`. `matchPoolJumpTable`: só destinos a até 4 KB do `jmp`; 4 tabelas novas (13 → 17). Testes `analysis_pool_*` |
| 2026-10-13 | **VRAM** (8 MB, área 1): caminho de 32 bits linear, de 64 bits intercalado entre bancos, espelhos `0x04/0x05/0x06/0x07` e de 8 MB | O boot zera `0xA5000000-0xA57FFFFF` (2 097 152 chamadas de `8C1C56BE`, uma por palavra). `runtime/pvr/vram.cpp`; testes `vram_*` |
| 2026-10-13 | Checkpoints de estado completo: `snap <F> --hit N --entry <arq> --vram` grava, na mesma sessão do Flycast, a entrada do jogo e o estado na N-ésima entrada de F; `bs-diff until` roda o recompilado até o mesmo ponto e compara registradores, FPU, 16 MB de RAM e 8 MB de VRAM (faixas divergentes listadas) | Primeira tentativa com entrada e checkpoint de sessões diferentes: 1 palavra em `0x8C000078` divergia por 44 min (hora do host gravada pelo reios no boot, `reios.cpp`); daí a exigência da mesma sessão |
| 2026-10-13 | **Estado completo idêntico ao Flycast depois de 2 097 270 chamadas** (checkpoint `8C1C6334`, 1ª entrada, logo depois da limpeza da VRAM): registradores, FPU, 16 MB de RAM e 8 MB de VRAM | `bs-diff until`: 0 divergências. Entra na regressão `boot_trace` (`cp_entry.bss` + `cp_8C1C6334_1.bss/.vram`) |
| 2026-10-13 | Polling do oráculo: interromper a cada 50 ms deixava o Flycast mais parado que rodando (cada `^C` recria a thread de emulação) | Espera exponencial até 2 s; o checkpoint depois de 2 milhões de chamadas sai em 7,6 s |
| 2026-10-13 | **Interrupções** (R1): INTC do SH-4 (ICR/IPR, INTEVT), IRL do Holly nos níveis 6/4/2 (IML*), entrada em VBR+0x600 como chamada aninhada (SSR/SPC/SGR, MD/RB/BL), retorno pelo `rte` conferido. Poll nos back-edges com o endereço de retomada e logo depois de `ldc ...,sr` | No Flycast a 1ª VBlank é aceita em `8C08BBB4`, logo depois de `ldc r0,sr` (SSR = 0x60000100). `runtime/cpu/sh4_intc.cpp`; testes `intc_*` |
| 2026-10-13 | Vetores de exceção do Katana são copiados pelo jogo para VBR+0x100/0x400/0x600 (`8C17C460`, origem `8C198140`/`8C198174`, 0x34 bytes) com o literal de destino corrigido na cópia | RAM do Flycast nesses endereços comparada com o executável. Analisador ganhou segmentos `[[relocated]]`: instruções analisadas no destino, pool lido da RAM, e o runtime confere as instruções antes de executar (`sh4_check_code`). Teste `analysis_relocated_*` |
| 2026-10-13 | **Pool não é constante**: o tratador de interrupção carrega a pilha de interrupção de `8C198478`, palavra do pool que o jogo grava (`8C19830E-8C198312`). Com o pool embutido, `r15` virava `FFFFFF1C` | `inline_pool_constants = false`: o código gerado lê o pool da RAM; a análise de fluxo ainda usa as constantes e os alvos são conferidos em tempo de execução |
| 2026-10-13 | Callbacks de interrupção alcançados só por ponteiro: prólogo "preparo + push" (`8C1C56D4`: `sts fpscr,r0; mov.l r0,@-r15`) e `stc/ldc sr` depois de prólogo (`8C17C1B8`) | Fatais em `8C1C5648` e `8C1C5708`. Heurística ampliada; aceitar qualquer privilegiada trouxe 2 falsos (dado com `ldc.l @r5+,spc`), por isso só `stc/ldc sr`. 3 942 funções, 0 fins mortos, 0 órfãs |
| 2026-10-13 | RTC do AICA (`0x00710000/04/08`) e controle `0x00702C00` | O jogo grava a hora lida em RAM (`0C00F344`, `0C2D8EE4`). No `bs-diff`, o RTC parte da hora gravada pelo reios em `0x8C000078`; o Flycast avança pela hora do host, então fica sempre alguns segundos à frente (`906D38C8` × `906D38C5`). Teste `aica_rtc_*` |
| 2026-10-13 | Checkpoint `8C094E28` (logo antes da 1ª syscall de GD-ROM), com interrupções: registradores, FPU e VRAM idênticos; 63 palavras de RAM divergentes | A 1ª VBlank entra numa função chamada em `8C08BBA8` no recompilado e no `ldc` de `8C08BBB2` no Flycast (fase do vídeo por ciclos estimados). Por isso esse checkpoint **não** entra na regressão estrita. Validar essa região exige reproduzir no recompilado as interrupções gravadas no Flycast (pendente) |

Regressão: teste `boot_trace` (CTest) refaz o trace recompilado e o compara em
modo estrito com o trace do Flycast; roda com `BS_SNAPSHOT_DIR` apontando para
`boot_entry.bss` + `boot_flycast3k.tsv` locais (SKIP sem ela). As únicas
diferenças aceitas estão em `tests/diff/boot_accepted.tsv`, cada uma com
chamada, função, coluna, os dois valores e o motivo; não há máscara global
(`--ignore` foi removido). Diferença aceita que deixe de ocorrer também falha.
Verificado que o teste falha ao remover uma linha da lista (2026-10-13).

Limitações conhecidas do trace diferencial: o snapshot de entrada não inclui
registradores de hardware (CCR, porta A, PVR); a comparação é só na entrada de
função (a RAM não é comparada no modo trace); a fase do vídeo depende dos ciclos
estimados e nunca coincide exatamente com o Flycast.

### M4: do HLE do BIOS ao primeiro STARTRENDER (2026-10-13/14)

Níveis de equivalência usados abaixo: **completa** = registradores, FPU, RAM e VRAM
idênticos ao Flycast; **parcial** = comparação com diferenças restantes explicadas;
**aproximação** = modelo que não pretende coincidir com o Flycast (tempo).

| Data | Item | Como foi verificado / nível |
|---|---|---|
| 2026-10-13 | HLE das syscalls do BIOS: vetores em `0x8C0000B0+` apontam para `0x8C001000+` (os mesmos endereços do reios); GD-ROM (INIT, REQ_CMD, GET_CMD_STAT, EXEC_SERVER, DRV_STAT, RESET; comandos INIT, GETTOC2, REQ_SES, PIOREAD/DMAREAD, GET_VERSION), FLASHROM (INFO/READ/WRITE/DELETE), SYSINFO (INIT/ICON/ID) | O boot lê o PVD (LBA 239371) e os arquivos do jogo. Testes `gdrom_*`, `flashrom_*`, `bios_sysinfo_via_vector`. DMAREAD é síncrono (o reios conclui por ticks): aproximação |
| 2026-10-13 | GD-ROM depende só de `DiscSource`; a fonte atual lê o CDI do usuário (`gdrom/image_source.cpp`) | Decisão do mantenedor (2026-10-14): CDI como fonte inicial |
| 2026-10-13 | Flash no modo diferencial: a do Flycast (`BS_FLASH_IMAGE`). Flash padrão própria para o boot nativo: **pendente** (autorizada em 2026-10-14) | — |
| 2026-10-13 | Store queues (SQ0/SQ1, QACR0/1, `pref`): RAM (área 3) e área 4 (TA, caminho direto de textura) | Teste `sq_flush_to_ram_and_to_ta` |
| 2026-10-13 | TA: parâmetros de 32/64 bytes conforme o tipo de polígono/vértice; fim de lista acende ISTNRM 7/8/9/10/21; `TA_LIST_INIT`/`TA_LIST_CONT` | Testes `ta_*`. O boot envia listas reais (global `80000000`, `81000000`, `82000000`; polígono translúcido `828C0002` com vértices `E0000000`/`F0000000`) |
| 2026-10-13 | Código copiado e callbacks: `8C17DE4C` (trampolim de volta de interrupção), `8C15AFC0`, `8C174888`, `8C17B04E` | Analisador: `bodyIsCode` valida o corpo inteiro de candidatos por ponteiro; prólogo nas 4 primeiras instruções; `stc/ldc` de SR/SSR/SPC aceitas só com prólogo. 4 038 funções, 0 fins mortos, 0 órfãs |
| 2026-10-13 | **Threads do guest** (o jogo troca de thread com `rte`, carregando SPC/SR e R15 de outro contexto): fibras do Windows, uma por thread; `ret_sp` identifica a pilha esperada | Aprovado condicionalmente pelo mantenedor em 2026-10-14. Testes `threads_*` (troca e retomada, pilhas distintas, continuação na mesma pilha, erro fatal numa fibra chegando à thread 0) |
| 2026-10-14 | Defeito achado pelo teste de FPU: depois de trocar de fibra, o arredondamento do host não seguia o FPSCR | Corrigido: `sh4_apply_host_fp_mode(fpscr)` em toda retomada e no início de cada fibra |
| 2026-10-14 | **Causa do laço de frames sem STARTRENDER**: o jogo espera a máscara de listas concluídas `[8C329B18] = 7` (opaca, modificador opaco, translúcida). No Flycast chegava a 7; no recompilado ficava em 1 | Snapshots no Flycast e no recompilado na 1ª entrada de `8C1C4C9E` (função que grava STARTRENDER), comparação das variáveis do jogo |
| 2026-10-14 | Primeira diferença causal: **momento de entrega da interrupção** de fim de lista opaca. No Flycast ela é aceita logo depois do `pref` (dentro de `8C18BB84`), e o tratador (`8C1C53E2`) dispara o DMA do canal 2 das outras listas; no recompilado ela só era checada no próximo poll, depois de `8C18BB84` já ter chamado a fila de DMA (`8C1C1E16`) | Traces de chamadas a partir da 1ª entrada de `8C18BC3C` (oráculo `trace --hit`, `bs-diff` com `BS_TRACE_FROM`), lado a lado |
| 2026-10-14 | Correção: checagem de interrupção logo depois de cada `pref` (como depois de `ldc sr`). Nenhuma flag do jogo alterada, nenhuma interrupção inventada | O jogo passa a disparar o DMA do canal 2 (`SB_C2DST = 1`, destino `0x10000000`, 64 bytes) |
| 2026-10-14 | DMA do canal 2 (SH-4 DMAC → TA): SAR2/DMATCR2/CHCR2/DMAOR; `SB_C2DST` transfere `SB_C2DLEN` bytes; no fim CHCR2.TE, C2DSTAT avança, C2DLEN/C2DST zeram e ISTNRM bit 19 | Teste `dmac_channel2_to_ta`. Transferência síncrona (aproximação de tempo) |
| 2026-10-14 | **Primeiro STARTRENDER real** (`0x005F8014 = FFFFFFFF`), depois das três listas concluídas pelo mesmo caminho do Flycast | Marco na regressão `boot_trace` (`BS_STOP_ON_STARTRENDER=1`, com disco e flash do modo diferencial): STARTRENDER alcançado com 2 threads do guest |
| 2026-10-14 | Estado na 1ª entrada de `8C1C4C9E` (3 161 595 chamadas) contra o Flycast: **parcial**. Registradores e FPU iguais exceto SSR (bit T da última interrupção); 767 palavras de RAM diferentes | Diferenças explicadas: Maple (buffers de resposta `0C27E1A0+`, `0C2B42xx`; o DMA do Maple não existe), TMU (`0C329CE4`, `0C1DD2xx`: contador lido como 0), hora do host (`0C000078`, `0C84EF40`), momento das interrupções (pilhas `0C2Dxxxx`). As variáveis de vídeo/listas do jogo coincidem, exceto as derivadas do TMU |

| 2026-10-14 | O stub GDB do Flycast lê os registradores do PVR (área 0) corretamente (ID, REVISION, SPG_LOAD conferem); `snap --pvr` grava `0x005F8000-0x005F9FFF` e `bs-diff until` compara registrador por registrador | Leitura de valores conhecidos |
| 2026-10-14 | **Defeito: endereços do SPG errados** (`SPG_VBLANK_INT` estava em `0x0C4`, `SPG_WIDTH` em `0x0C8`, `SPG_VBLANK` em `0x0CC`; corretos: `0x0CC`, `0x0E0`, `0x0DC`). As linhas de VBlank eram lidas de um registrador nunca escrito | Achado comparando o banco do PVR com o Flycast. Corrigido; vsync (bit 13) de `vstart` a `vbend`, campo (bit 10) alternando com `SPG_CONTROL.interlace`, bit 11 não aceso (como no Flycast) |
| 2026-10-14 | **Estado inicial do hardware (PVR)**: o boot lê `SPG_VBLANK` antes de escrevê-lo; com zero, o laço de vsync em `8C172620` nunca terminava | Banco do PVR do Flycast na entrada do jogo (`snap 0x8C010000 --pvr`), carregado pelo `bs-diff` (`<entrada>.pvr`). A fase do feixe no ciclo 0 vem do `SPG_STATUS` desse estado (`0x424`: linha 36, campo 1). Valores não nulos abaixo |
| 2026-10-14 | Com a fase inicial, `r3` em `8C172634` (fim do vsync) passou a coincidir exatamente com o Flycast; `r2` (1ª leitura com vsync) difere pela entrada no laço já dentro do vblank | Diferença aceita atualizada em `tests/diff/boot_accepted.tsv` (3 aceitas, eram 4) |

Registradores do PVR deixados pelo BIOS (reios do Flycast) na entrada do jogo,
valores não nulos (`0x005F80xx`): `08`=7, `30`=101, `40`=C0BEBC, `44`=D, `48`=6,
`4C`=140, `50`=200000, `54`=200A00, `5C`=2813BE7F, `60`=600000, `64`=600A00,
`68`=27F0000, `6C`=1DF0000, `74`=1, `78`=3F800000, `7C`=27DF77, `80`=7,
`88`=38D1B717, `8C`=1000000, `98`=402000, `A0`=20, `A4`=1F, `A8`=15F28997,
`B0`=7F7F7F, `B4`=7F7F7F, `B8`=FF07, `BC`=FFFFFFFF, `C0`=FF000000, `C8`=3450000,
`CC`=150104, `D0`=150, `D4`=7E0345, `D8`=20C0359, `DC`=240204, `E0`=7D6C63F,
`E8`=FF0000, `EC`=9D, `F0`=150015, `F4`=400, `10C`=424, `110`=93F39, `118`=8040,
`11C`=FF, `124`=C2680, `12C`=9E800, `130`=9E740, `134`=C2680, `13C`=E0013,
`140`=100203, `164`=C2680. O boot nativo precisa reproduzir este estado.

Pendentes para o próximo avanço: o jogo espera a interrupção de render concluído
depois do STARTRENDER (ainda não gerada); TMU; DMA do Maple; flash padrão para o
boot nativo; estado inicial do PVR no boot nativo.

#### Evidência de que o STARTRENDER foi alcançado naturalmente

- Nenhuma escrita em STARTRENDER, flag do jogo ou registrador é feita pelo runtime;
  o valor `0xFFFFFFFF` é gravado pelo código do jogo (`8C1C4DF4`, função `8C1C4C9E`).
- A condição do jogo para gravar é o estado do frame (`[8C329AFC] = 3`), que só sai
  de `8C1C512E` quando a máscara de listas concluídas `[8C329B1C]` chega a
  `[8C329B18] = 7`. Os bits vêm do tratador de interrupção do próprio jogo
  (`8C1C53E2`, bits `0x80`/`0x100`/`0x200` do ISTNRM), acesos pelo nosso TA ao receber
  os fins de lista que o jogo enviou (opaca por store queue; modificador opaco e
  translúcida pelo DMA do canal 2).
- Todas as interrupções entregues correspondem a eventos produzidos pelo hardware
  modelado (VBlank pela posição do feixe, fim de lista pelo TA, fim de DMA pelo
  canal 2). A única mudança que destravou o boot foi o **momento** da checagem (logo
  depois de `pref`), que reproduz a ordem observada no Flycast (traces lado a lado a
  partir de `8C18BC3C`).
- O marco é verificado a cada execução da regressão (`boot_trace`, com
  `BS_STOP_ON_STARTRENDER=1`): build debug e build release limpo.

#### Limitações conhecidas (2026-10-14)

- **Equivalência completa** com o Flycast só até a chamada 2 097 270 (checkpoint
  `8C1C6334`) e nas 3 000 primeiras chamadas (com 3 diferenças aceitas). Depois das
  interrupções, a comparação é **parcial**.
- **Tempo é aproximado**: ciclos estimados por bloco, campo de vídeo fixo em 59,94 Hz,
  DMA (GD-ROM e canal 2) síncrono, interrupções só nos pontos de poll.
- Não implementados: TMU (o contador lê 0), DMA do Maple (buffers de resposta vazios),
  interrupção de render concluído, rasterização, áudio (ARM7/AICA além de RTC e RAM).
- Flash: só a do Flycast no modo diferencial; a flash padrão própria para o boot
  nativo ainda não existe. Estado inicial do PVR: só a captura do Flycast no modo
  diferencial; o boot nativo ainda não o reproduz.
- O snapshot não tem SGR, DBR nem XF (o stub GDB do Flycast não os expõe).
- `GET_CMD_STAT` com id errado e `REQ_SES` seguem o reios, sem outra referência.

#### Verificações antes do commit (2026-10-14)

- Build limpo: `cmake --preset msvc-release -B build/verify-release` num diretório novo
  compila tudo (84/84) sem erros; todos os testes passam nele e no debug, inclusive
  `boot_trace` com disco, snapshots e flash reais.
- Fibras: teste `threads_preserve_xmm6_15_and_rounding_across_fibers` usa uma rotina em
  assembly (`tests/unit/xmm_probe.asm`) que carrega sentinelas em XMM6-XMM15 e troca de
  fibra duas vezes; passa em debug e release. Sensibilidade comprovada por experimento
  temporário (corromper XMM6-XMM15 faz o teste falhar; desfeito, hash do código
  conferido). Observação: o teste também passa sem `FIBER_FLAG_FLOAT_SWITCH` (no x64 o
  `SwitchToFiber` já preserva XMM6-XMM15); o flag fica, conforme a documentação da
  Microsoft. O teste de arredondamento achou o defeito real antes da correção.

## Revisões independentes (Codex)

### 2026-10-14: revisão complementar (SPG corrigido, fase inicial, banco do PVR, FPU nas fibras)

Verificados sem problema: endereços do SPG iguais aos do Flycast; bit 11 não aceso;
campo só com entrelaçado; fase inicial dá linha 36/campo 1 no ciclo 0; fase aplicada
igualmente à posição e aos cruzamentos; `8C172634` não lê `r2` (bytes conferidos).

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [MÉDIA] Fibras sem `FIBER_FLAG_FLOAT_SWITCH`: XMM6-XMM15 (preservados na ABI x64) podem estar vivos numa fibra suspensa | **Aceito** | `ConvertThreadToFiberEx`/`CreateFiberEx` com `FIBER_FLAG_FLOAT_SWITCH`; o modo de FP continua reaplicado do FPSCR depois de cada troca (o flag restaura o MXCSR antigo da fibra). Teste com valores `double` vivos atravessando a troca (em debug o compilador pode mantê-los na pilha: teste mais fraco nesse modo) |
| [BAIXA] `.pvr` curto ou ilegível aceito em silêncio (completado com zeros) | **Aceito** | `bs-diff` exige exatamente 8 192 bytes na entrada e no esperado; senão, erro |

Depois: todos os testes passam (3 000 chamadas, checkpoint igual, marco STARTRENDER).

### 2026-10-14: revisão completa das mudanças pendentes desde `ac0ccec` (fibras, RTE, FPU, exceções, HLE, store queues, TA, interrupções, heurísticas)

Pontos verificados sem problema pelo Codex: `BS_CALL` e `switchTo` preservam
`ret_expect`/`ret_sp`; exceções não atravessam fibras; `rte` restaura SR antes do
slot; troca de banco por MD+RB; entrada de interrupção com SR/SPC/SGR; checagem
depois de `ldc sr` e `pref`; modo de FP reaplicado nas trocas; store queues;
stubs realocados; nenhum endereço do jogo comprovadamente mal classificado pelas
heurísticas.

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [MÉDIA] GETTOC2 junta todas as faixas na área 0 e devolve área 1 vazia; em GD as áreas são separadas por sessão | **Rejeitado para o CDI** | O disco do usuário é CD (CDI, faixas Mode 2). O Flycast monta o TOC desse disco exatamente assim (`libGDR_GetToc`: CD → faixas 1..N na área 0; área 1 → sem dados, status OK). O boot lê o sistema de arquivos e chega ao STARTRENDER. Teste `gdrom_toc_high_density_area_is_empty_for_cd`. Ao entrar GDI, `DiscSource` precisa informar o tipo de disco |
| [MÉDIA] O TA usa o bit 6 do PCW para "dois volumes"; o bit 7 seria o habilitador | **Rejeitado** | O tamanho com dois volumes depende do bit 6 (`Volume`; modo normal do modificador no KallistiOS); o bit 7 (`Shadow`) só habilita o modificador. O Flycast usa o bit 6 em `poly_header_type_size`/`poly_data_type_id`, e as listas reais do jogo são interpretadas sem desalinhamento. Teste `ta_two_volume_size_comes_from_bit6_not_bit7` |
| [BAIXA] `GET_CMD_STAT` com id diferente enquanto ocupado devolve "ocupado" | **Mantido (comportamento do oráculo)** | O reios faz o mesmo; não há evidência de que o jogo consulte id errado. Revisitar se aparecer divergência |

Depois: todos os testes passam (regressão de 3 000 chamadas, checkpoint igual, marco STARTRENDER).

### 2026-10-13: M4, segunda etapa (VRAM, checkpoints, interrupções, código realocado). Relatório em `reviews/codex-20261013-213310.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Interrupção aceita tarde: sem poll em trechos sem laço e sem nova checagem depois do `rte` | **Aceito** | `sh4_irq_check` reavalia depois que o tratador volta (fonte ainda pendente é aceita antes da instrução em SPC; manual SH7750 19.6), com limite para tratador que nunca reconhece. Poll também no retorno de cada chamada (`BS_CALL`) |
| [MÉDIA] Checkpoint chamado de "estado completo" sem SGR, DBR, XF, PC, ciclos e hardware | **Aceito** | `until` passa a exigir o mesmo PC (endereço físico) e se declara comparação **parcial**; SGR/DBR/XF não saem pelo stub GDB do Flycast. VRAM de entrada: o snapshot de entrada é anterior a qualquer escrita na VRAM (o boot a zera) |
| [MÉDIA] Tabela com base no pool pode ser truncada por `cmp` não relacionado ou ser falsa | **Verificado, sem mudança** | As 4 tabelas desse tipo são sem limite (fim pela heurística, não por `cmp`). As três do memcpy (`8C08D1D8/1E4/280`, 17 casos) batem com `r0 ≤ 64` do chamador `8C08D274`; `8C18864E` tem 2 casos e `r0 = r14 ∈ {0,1}` (`8C188640/42`). Alvo fora da tabela cai no despacho (fatal), nunca em silêncio |
| [BAIXA] Heurística de prólogo ampliada pode aceitar dado | **Aceito em parte** | Achado um defeito real no caminho: `sts.l mach/macl/pr,@-Rn` contava como prólogo para qualquer `n` (falso positivo `8C130594`). Agora exige `r15`; teste negativo `analysis_pointer_candidate_rejects_push_off_stack`. 3 972 funções, 0 fins mortos, 0 órfãs |

Depois das correções: todos os testes passam, regressão `boot_trace` (3 000 chamadas + checkpoint `8C1C6334`) igual.

### 2026-10-13: M4 início (oráculo, trace diferencial, primeiros registradores). Relatório em `reviews/codex-20261013-202912.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Oráculo abria Flycast sem checar a porta: com outro já escutando, conectaria ao antigo e deixaria o novo órfão | **Aceito** | `connect()` testa a porta antes do `Popen` e aborta (sugere `--attach`) |
| [ALTA] Snapshot sem ciclos nem estado de hardware | **Aceito como limitação** (já registrada) | A fase do vídeo e os registradores P4/Holly do Flycast não saem pelo stub GDB (leitura de `PDTRA` via GDB devolve 0, o jogo lê 0x300). `bs-diff` agora avisa; o primeiro caso visível é `VO_CONTROL` lido antes de escrito. Revisitar ao testar funções isoladas com `call` |
| [MÉDIA] `call` gravava `exit.bss` mesmo sem achar o retorno em 1000 paradas | **Aceito** | `for ... else` aborta sem gravar |
| [MÉDIA] `--ignore r0-r3` e PR em espelho P2 podem esconder divergência | **Aceito em parte** | O modo estrito continua o padrão; cada diferença ignorada ou espelhada agora é listada. Nas 600 chamadas: só 2 ignoradas (fase do `SPG_STATUS` em r2/r3) e 2 de espelho |
| [MÉDIA] Reprogramar SPG aplicava a configuração nova a tempo já passado | **Aceito** | `holly_intc_sync()` antes de escrever `SPG_LOAD`/`SPG_VBLANK_INT`. Teste `holly_istnrm_spg_reprogram_is_not_retroactive` |
| [MÉDIA] Registradores sem modelo tratados como latch | **Aceito em parte** | Devolver 0 também não tem base; mantido o latch, mas PVR e SB registram a primeira leitura sem modelo de cada endereço |

Depois das correções: todos os testes passam; trace de 600 chamadas continua idêntico.

### 2026-10-13: M3 (semântica, analisador, gerador). `codex exec` read-only; relatório em `reviews/` (não versionado)

| Achado | Decisão | Evidência / ação |
|---|---|---|
| `rte` ignorava SPC | **Aceito** | Gerador lê SPC antes do slot, restaura SR, executa o slot e verifica o retorno (`BS_RET`). Teste `codegen_rte_uses_spc` |
| FPSCR.DN sem efeito (ARCHITECTURE prometia FTZ/DAZ) | **Aceito** | `sh4_set_fpscr` liga FTZ+DAZ no MXCSR com DN=1. Teste `ops_fpscr_dn_flushes_denormals` |
| Campos Cause/Flag/Enable do FPSCR não atualizados | **Limitação registrada** | Exige exceções de FPU; nenhuma evidência de que o jogo os leia. Revisitar se aparecer `sts fpscr` seguido de teste dos bits |
| Troca de banco R0-R7 ignorava MD | **Aceito** | Banco efetivo = MD && RB. Teste `ops_register_bank_depends_on_md_and_rb` |
| Word/longword desalinhado aceito no caminho rápido | **Aceito** | Desalinhado vai ao caminho lento, que acusa address error (fatal com PC e endereço) |
| PC-relativa em delay slot aceita | **Aceito (conservador)** | Não deu para confirmar a regra do manual offline; o jogo tem 0 ocorrências. Agora é "não suportado": aviso na análise e fim morto |
| Tabela `0x8C04E4BC` limitada só por heurística | **Aceito** | Confirmado no disassembly (`mov.w` do pool = 0x00AC = 172). `boundBefore` resolve constantes do pool |
| FSCA/FSRRA/FIPR/FTRV não são bit a bit iguais ao hardware | **Risco já registrado (R5)** | Exige referência de hardware (oráculo Flycast nos testes diferenciais); sem evidência para corrigir agora |

Depois das correções: todos os testes passam, análise idêntica (3 883 funções, 0 fins mortos, 0 órfãs), C++ gerado recompila e `bs-gen-check` passa.

## Em aberto

- Instrução PC-relativa em delay slot (`mova`/`mov.x @(disp,PC)`): o decodificador calcula o alvo a partir do PC da própria instrução. A regra especial do manual para delay slot é tratada no M3.
- D7 (distribuição), decidir no M8.
- clang-cl não está instalado nesta máquina. Por enquanto só o MSVC é testado.
