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
- Não implementados: TMU (o contador lê 0), áudio (ARM7/AICA além de RTC e RAM).
  Depois do commit `ada8d9a` vieram fim de render (tempo aproximado), rasterizador de
  referência e Maple (controle, sem VMU): ver a seção seguinte.
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

### M4: rasterizador de referência, comparação de framebuffer e Maple (2026-10-14)

**Rasterizador (`runtime/pvr/pvr_render.cpp`, `pvr_texture.cpp`)**: roda inteiro na
escrita de STARTRENDER, a partir dos parâmetros recebidos pelo TA; grava em FB_W_SOF1
no formato de FB_W_CTRL. Fundo pelo plano de ISP_BACKGND_T/D, listas opaca,
punch-through e translúcida, profundidade 1/w, regra top-left, perspectiva correta,
texturas 16 bpp e paleta 4/8 bpp (twiddled/linear), blend do TSP.
- Testes: `pvr_twiddle_*`, `pvr_texel*`, `render_*` (fundo, cobertura por centro de
  pixel, regra de preenchimento nos dois sentidos, profundidade).
- **Aproximações**: sem fog, dither, mipmap, VQ, YUV, bump, dois volumes, volumes
  modificadores, escala do SCALER_CTL. Formatos sem suporte são registrados e omitidos
  (contador `omitidos`). O autosort por pixel foi implementado depois (seção seguinte).
- Ferramentas: `tools/dev/fb_png.py` (framebuffer → PNG e diferença pixel a pixel;
  `--read` lê o buffer exibido, `--sof` escolhe o endereço); `BS_DIFF_SAVE` grava
  também `.vram` e `.pvr`; diagnósticos `BS_VRAM_WATCH` e `BS_PVR_WATCH`.

**Comparação com o Flycast** (oráculo com `rend.EmulateFramebuffer = yes` no `emu.cfg`
local da ferramenta, não em configuração global):
- No 41º STARTRENDER, VRAM igual exceto os marcadores de OPB do Flycast e o plano de
  fundo em VRAM 0, que o jogo grava em `8C1C2066` a cada quadro. No Flycast esse código
  ainda não tinha rodado: o ritmo de quadros diverge (FB_R_CTRL=1 no recompilado, 0 no
  Flycast).
- Primeiro framebuffer produzido pelo recompilado (STARTRENDER 150 e 400): a tela "VMU
  not found / Press START", 304 triângulos de texto, sem formato omitido.
  **Não é resultado válido de equivalência.** O caminho até ela dependia da ausência
  do Maple (ver abaixo), e o framebuffer do Flycast em VRAM saiu todo zero nos pontos
  capturados.
- Causa da divergência (checkpoint pareado na 1ª entrada de `8C1C4914`, a rotina de
  flip): RAM diferente nos buffers do Maple (`0C27E1A0`, `0C2841A0`, `0C2845A0`...). O
  Flycast responde com controle + 2 VMUs; o recompilado não tinha Maple (buffers
  zerados e nenhuma interrupção de fim de DMA, o que o hardware nunca produz).

**Maple (`runtime/maple/maple.cpp`)**: DMA pela tabela de SB_MDSTAR, quadros, "sem
resposta" = 0xFFFFFFFF, fim em ISTNRM bit 12, SB_MDST ocupado durante a transferência.
Controle padrão na porta A, sem VMU. Os dados do controle foram conferidos com as
respostas que o jogo recebe no Flycast. O tempo (100 ciclos por bit, 2 Mbit/s) é
aproximado. Testes: `maple_*` (5).
- Com o Maple modelado, com controle ou sem dispositivo nenhum (`BS_MAPLE_PORTS=none`),
  o jogo chega ao escalonador cooperativo da biblioteca de threads. Ele usa setjmp
  (`8C099F8C`) e longjmp (`8C099F18`): o yield em `8C09037E` faz
  `if (setjmp(atual) == 0) longjmp(escalonador, 1)`, o escalonador em `8C0903C0` faz
  longjmp para o contexto de cada thread, e threads novas começam pelo `rts` do longjmp
  num contexto com PR = entrada (`8C09081A`) e pilha nova. O runtime para com fatal
  "retorno para endereço inesperado" (`8C099F84` → `8C09081A`).
- **Decisão do mantenedor (2026-10-14)**: suporte genérico a longjmp (ARCHITECTURE.md),
  e nos testes diferenciais o controle sem VMU dos dois lados. No oráculo isso fica no
  `emu.cfg` local da ferramenta: `[input] device1.1 = device1.2 = 10` (MDT_None).
- Implementado: `[threads] setjmp = [8C099F8C]`, `longjmp = [8C099F18]`. O gerador cria
  5 funções com pontos de reentrada, e o runtime tem `sh4_longjmp_redirect`. Testes:
  `longjmp_switches_between_scheduler_and_worker_fibers` (escalonador → thread nova →
  yield → retomada → fim, com as macros do código gerado), `longjmp_to_unknown_target_is_fatal`,
  `codegen_setjmp_return_is_reentry_label`.
- Resultado no boot: 2 threads cooperativas criadas (`8C09081A`, `8C09081E`), 1 236
  trocas por longjmp até o 150º STARTRENDER, 4 threads do guest no total. A tela
  renderizada é "VMU not found", agora por um caminho coerente com a configuração
  (controle sem VMU).
- Regressão depois da mudança: todos os testes passam (84 unitários); `boot_trace` com
  3 000 chamadas, checkpoint `8C1C6334` igual e marco STARTRENDER ok.

**TMU (`runtime/memory/sh4_tmu.cpp`)**: comparação pareada no 3º STARTRENDER
(controle sem VMU nos dois lados, entrada `nv3_entry`). O jogo guarda leituras do TMU0
em `0C1DD20C`/`0C1DD224`: no Flycast `0x4B4ABF`..., no recompilado 0 e 0xFFFFFFFF,
porque o TMU não existia. Implementados os três canais (TOCR, TSTR, TCOR/TCNT/TCR,
TCPR2), contagem decrescente derivada de `c->cycles` (4 ciclos da CPU por tick de
P = 50 MHz, divisores 4/16/64/256/1024), recarga e UNF. Interrupção de underflow e
fontes RTC/externa ficam registradas, sem modelo (o jogo usa TCR0 = 2, sem UNIE).
Testes: `tmu_channel0_free_running_counter`, `tmu_underflow_reloads_and_sets_unf`.

**Divergência ainda aberta (tempo e oráculo)**:
- No 3º STARTRENDER, o Flycast tinha 281 VBlanks (`0C1DD1C4` = 0x119) e o recompilado
  22. O TMU confirma a diferença: ~6 s contra ~0,6 s. Causa provável: o Flycast emula a
  velocidade de leitura do GD-ROM, e o HLE do GD-ROM daqui termina as leituras na hora.
  Pareamento por número de STARTRENDER deixa de ser comparável depois dos primeiros
  quadros.
- No oráculo, o buffer 0 fica no estado 4 (tabela `8C329AFC`) do 1º ao 150º
  STARTRENDER, o flip (`8C1C4914`) nunca liga o vídeo (FB_R_CTRL = 0, VO_CONTROL com
  blank) e a VRAM do framebuffer fica zerada, mesmo com `rend.EmulateFramebuffer`. No
  recompilado, os buffers giram (estados 7/4) e a imagem aparece. Ainda não está claro
  se é comportamento real ou artefato do Flycast parado pelo GDB (render em thread).
  Com `rend.ThreadedRendering = no` o Flycast fecha a conexão GDB, e a opção foi
  revertida. Até resolver isso, a comparação de framebuffer com o Flycast **não está
  disponível**, e nenhum framebuffer daqui é declarado equivalente. → **Resolvido
  abaixo** (artefato da captura por breakpoint).

### M4: oráculo gráfico e validação do rasterizador (2026-10-14)

**Causa do framebuffer zerado no Flycast: a captura, não o Flycast.**
- Interrupção de fim de render: depois do 1º STARTRENDER, o tratador Holly do jogo
  (`8C1C53E2`, que lê ISTNRM em r13) recebeu ISTNRM = 0x17 (bits 0-2 + VBlank). O fim de
  render chega ao jogo no Flycast. O tratador só passa o buffer para o estado 5
  (`8C1C54BC`) com o bit 2 aceso.
- Execução livre: o jogo rodou sem breakpoints, parado por ^C a cada 30 s (script de
  experimento fora do repositório). A partir do VBlank ~371, o Flycast gira os buffers
  (estados 5/3/7), liga o vídeo (FB_R_CTRL = 1, VO_CONTROL sem blank) e alterna
  FB_R_SOF1, como o recompilado. Os 15 min de execução e 3 535 VBlanks foram estáveis.
- Conclusão: as paradas repetidas na instrução que escreve STARTRENDER (`snap --hit N`
  com breakpoint em `8C1C4DF8`) interferem no PVR do Flycast (o stub chama `emu.stop()`
  e `emu.start()` em cada parada, e o render roda em outra thread). O mecanismo exato
  dentro do Flycast **não foi verificado**. Nada foi alterado no Flycast.
- Ferramenta: `flycast_oracle.py snap-free <arquivo> --seconds N` (sem breakpoint depois
  da entrada do jogo; grava `.bss`, `.vram`, `.pvr`). O jogo no oráculo roda a ~2
  VBlanks por segundo de host (interpretador, imposto pelo stub). O 1º STARTRENDER do
  Flycast ocorre no VBlank 282, contra ~20 no recompilado (ver tempo, abaixo).

**Primeiro framebuffer válido do Flycast e comparação** (`snapshots/fr_200`, parada
livre após 200 s, VBlank 681, tela "VMU not found"; recompilado no 150º STARTRENDER,
mesma tela estática, controle sem VMU nos dois). `fb_png.py` com o buffer exibido
(FB_R_SOF1 = 0x200000), 640x480 em 0555:
1. 1ª comparação: 48,2% dos pixels diferentes. Retângulos escuros atrás de cada linha de
   texto apareciam só no recompilado. Causa (por `BS_RENDER_PROBE`): o painel da janela e
   o texto são translúcidos (TSP 949004ED); o texto vem antes e tem z-write. Desenhado na
   ordem de submissão, as células transparentes do texto gravavam z e o painel falhava
   embaixo delas. O jogo usa ISP_FEED_CFG = 0x00800408 (bit 0 = 0, **autosort**).
   **Corrigido**: autosort por pixel (fragmentos ordenados por 1/w, do mais distante para o
   mais próximo, sem z-write dos translúcidos). Teste `render_translucent_autosort_vs_presort`.
2. Depois da correção: 36,42% diferentes, sendo **35,63% de no máximo 1 passo de
   quantização** e 0,79% maiores.
   - Os de 1 passo formam um xadrez 2x2 (0x0001/0x0021) no fundo da janela: **dither**
     (FB_W_CTRL bit 3 = 1), que o recompilado não modela. O dither do Flycast é um
     modelo próprio (tabela 4x4 num shader GPL) e não foi conferido com hardware. Fica
     como aproximação registrada, sem copiar o modelo do Flycast.
   - Os maiores (2 422 pixels) ficam só nas bordas dos glifos. Neles a coordenada de
     textura cai **exatamente na fronteira entre texels** (u·256 = 194,00000; 250,00001),
     então o texel escolhido depende da precisão numérica. Um experimento temporário
     (empate para o texel de baixo) reduziu os de 28-29 passos de 1 202 para 361, mas
     ainda não igualou o Flycast, cuja escolha vem do float da GPU, não do hardware.
     **Revertido**; fica registrado como ambiguidade sem referência de hardware.
   - Janela, borda, fundo e fora da janela: iguais ou dentro de 1 passo.
- Paleta (PAL_RAM_CTRL 2, entradas 0/FFFF/F111) e o atlas de fonte 4 bpp (256x256 em
  0x59D000 e 0x5A5000) são **idênticos byte a byte** nos dois lados.

**Validação do rasterizador** (`tests/unit/test_render_formats.cpp`, valores esperados
calculados à mão a partir dos formatos documentados):
- formatos 565/4444/1555 (limiar)/888/0888 (K)/8888; gouraud e flat; culling por
  orientação; strip; z-write desligado; os 8 compares de profundidade; blend SRC_ALPHA,
  os 8 fatores de origem e de destino; punch-through com PT_ALPHA_REF; textura com ponto
  e UV16; bilinear com meio texel; repetir/clamp/espelhar; paleta 8 bpp com seletor;
  textura linear com stride; instruções decal/modulate/decal alpha/modulate alpha, offset
  e ignore texture alpha; cor float e intensidade 1/2; sprite (cobertura e 4º vértice
  pelo plano); HALF_OFFSET; color clamp; autosort contra pre-sort; FB_X_CLIP e stride
  na VRAM.
- Linha de base de regressão: hash FNV-1a da cena composta `0x1797BA480B2C33A5` (gerado
  pela implementação atual e conferido visualmente; **não é evidência de equivalência**).
- Correções da revisão Codex (abaixo): 4º vértice do sprite pelo plano de A, B, C (antes
  A + C − B), HALF_OFFSET bits 0-2, color clamp do TSP (bit 21).
- `pvr_render.cpp` foi reconstruído depois de uma perda acidental (truncado por um
  script). A revisão completa do Codex sobre o arquivo reconstruído está abaixo.
- Total: 116 testes unitários; `boot_trace` com disco real passa.

**Segunda revisão do rasterizador** (manual Dreamcast/Dev.Box 3.4.2-3.4.3; tabela de
achados abaixo, em Revisões):
- Autosort: compare do polígono ignorado e forçado em `>=` contra as listas opacas.
- Punch-through: ordenado por pixel da frente para trás, compare `>=`; vale o fragmento
  mais próximo que passa no alfa (os que falham revelam o seguinte). A regra de empate
  ("último submetido vence") **não foi verificada**.
- Region Array tipo 2 (modo por tile): sem modelo. Se algum tile pedir modo diferente
  do global, o rasterizador avisa. Na captura do Flycast, 300 tiles, todos em autosort.
- Cores em float (vértice, face, intensidade) saturadas em [0, 1] e quantizadas em 8
  bits, como o TA as guarda. Achado ao passar de START: as cenas 3D usam offset em float
  negativo, e sem a saturação o offset subtraía e o quadro ficava preto. Arredondamento
  para o mais próximo, **não verificado** contra o hardware.
- `fb_png.py`: limiar de "1 passo" por formato (4444: 17 em 8 bits).
- Testes: `render_autosort_forces_gequal`, `render_punch_through_front_to_back`,
  `render_sprite_fourth_vertex_uv_perspective`, `render_half_offset_attribute_sample`,
  `render_half_offset_texel_center`, `render_negative_float_offset_saturates_to_zero`.
  Total: 126 unitários. A comparação da tela "VMU not found" com o Flycast não mudou
  (36,42% / 35,63% em 1 passo / 0,79% maiores).

### M4: controles (entrada) e primeira tela além de "Press START" (2026-10-14)

- `runtime/input/` (independente do Maple): `PadState` (botões em lógica positiva,
  analógico, gatilhos) e fontes:
  - teclado do Windows (`GetAsyncKeyState`) com mapa `BOTAO = TECLA`. Padrão: setas,
    ENTER = START, Z/X/A/S = A/B/X/Y, Q/W = gatilhos, I/J/K/L = analógico;
  - XInput (primeiro controle conectado);
  - roteiro determinístico `BS_INPUT_SCRIPT`, com linhas `<n> <botões|->`, onde `n` é a
    n-ésima leitura de condição feita pelo jogo à porta A (não depende do relógio).
- Ativação: `BS_INPUT_SCRIPT=<arquivo>`, ou `BS_INPUT=host` (teclado + XInput,
  `BS_INPUT_MAP` opcional). Sem nada disso, nenhuma entrada: os testes diferenciais
  continuam determinísticos.
- O Maple traduz o estado da fonte na condição do controle (ativo em 0, analógico com
  centro em 0x80). VMU continua fora.
- Testes: `input_button_names`, `input_script_is_deterministic_by_read_sequence`,
  `input_script_and_keymap_errors`, `input_script_drives_maple_get_condition`.
- **Resultado**: com o roteiro `200 START` / `230 -`, o jogo passa da tela "VMU not
  found". Depois vêm ~360 quadros de 2 a 4 triângulos e então cenas 3D de 6 400 a 7 000
  triângulos, sem formato omitido. No 700º STARTRENDER, o framebuffer mostra a **tela de
  título** (logotipo BLUE STINGER e personagem 3D com partículas). Ainda **sem
  comparação com o Flycast** nessa tela: para isso é preciso mandar START ao oráculo pela
  entrada dele.
- Limitação: ainda não há executável nativo interativo (janela + apresentação do
  framebuffer). As fontes de teclado e XInput existem e estão testadas na leitura, mas
  só podem ser usadas fora do `bs-diff`, que as recusa por não serem determinísticas.

### M4: tempo do GD-ROM (2026-10-14)

Separação das causas da divergência de VBlanks (1º STARTRENDER no VBlank 282 no Flycast,
18 no recompilado):
- **Depuração**: descartada. Na execução livre (sem breakpoints), o Flycast também só
  começa a renderizar entre os VBlanks 252 e 371.
- **Leitura de disco**: até o 1º STARTRENDER, o jogo lê 7,7 MB (74 leituras, 3 781
  setores). O oráculo usa o BIOS HLE do Flycast (reios). Nele, cada leitura por DMA anda
  em blocos de 5 setores, de 1 000 000 de ciclos cada enquanto restam mais de 5 setores
  (~2 MB/s), e o último bloco leva 2 ciclos por byte. Daqui, tudo terminava na hora.
- **Escalonamento/ciclos**: o resto. O contador de ciclos daqui é uma estimativa por
  bloco, e o do Flycast vem do interpretador dele.

Modelo implementado (`runtime/gdrom/gdrom.cpp`), configurável por
`BS_GDROM_TIMING=instant|reios` (padrão `instant`, o comportamento validado até aqui):
`reios` reimplementa o modelo do oráculo descrito acima (comportamento, nenhum código
copiado). É o modelo do oráculo, não o do hardware, e serve para comparações
determinísticas. A transferência avança pelo poll central (`sh4_service` →
`gdrom_poll`), sem depender de nova syscall, como o scheduler do Flycast. Um pedido novo
descarta a transferência antiga (como o reios). Testes
`gdrom_reios_timing_reads_in_timed_blocks` (12 setores: blocos em +1 000 000,
+2 000 000 e +2 008 192 ciclos; `result[2]` progressivo) e
`gdrom_reios_timing_edge_cases` (5 e 6 setores, 0 setores, RESET seguido de pedido novo,
erro no meio de um bloco, avanço sem syscall).
- Resultado: com `reios`, o 1º STARTRENDER vem no **VBlank 237** (Flycast 282, `instant`
  18). A diferença cai de 264 para 45 VBlanks: o disco explica (237 − 18) / (282 − 18) ≈
  **83%** dela. O resto fica para a estimativa de ciclos da CPU.
- Na comparação gráfica, o critério continua sendo **estado equivalente** (mesma tela
  estática, captura livre) e não "o quadro N". As capturas do Flycast com breakpoint em
  STARTRENDER (`sr_*`, `nv_*`) estão contaminadas pela interferência descrita acima e não
  servem para comparar o PVR.

## Revisões independentes (Codex)

### 2026-10-14: modelo de tempo do GD-ROM. Relatório em `reviews/codex-20261014-111438.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [MÉDIA] A transferência só avançava em syscalls de GD-ROM | **Aceito** | `gdrom_poll` no poll central (`sh4_service`). Teste: buffer pronto sem syscall |
| [MÉDIA] RESET seguido de pedido novo herdava a transferência antiga | **Aceito** | REQ_CMD zera a transferência (como `multi_read_count = 0` no reios). Teste com RESET no meio de uma leitura |
| [BAIXA] Teste sem bordas | **Aceito** | `gdrom_reios_timing_edge_cases` |
| [BAIXA] "~84% da diferença" com a razão errada | **Aceito** | (237 − 18) / (282 − 18) ≈ 83%. Corrigido |

Depois das correções: 131 testes unitários e `boot_trace` com disco real passam. O 1º
STARTRENDER com `reios` continua no VBlank 237.

### 2026-10-14: controles (entrada), cores float, punch-through. Relatório em `reviews/codex-20261014-110826.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [MÉDIA] Punch-through respeitava o z-write disable | **Aceito** (evidência de compatibilidade, não do manual) | Registro do DEmul ("ignore z-write disable for punch-through", Bust-A-Move 4). O punch-through sempre grava a profundidade. Teste `render_punch_through_ignores_zwrite_disable`. Uso pelo Blue Stinger não verificado |
| [BAIXA] `bs-diff` aceitava entrada ao vivo (não determinística) | **Aceito** | `BS_INPUT=host` recusado (saída 2), salvo com `BS_DIFF_LIVE_INPUT=1`, que avisa que o resultado não é determinístico |
| [BAIXA] Serialização do Maple só testada com START | **Aceito** | Teste `input_maple_serializes_every_button_and_axis`: os 12 botões, gatilhos R/L e analógicos assimétricos |
| [BAIXA] Dispositivo principal respondia ao endereço de outra porta | **Aceito** | Exige destino `(porta << 6) \| 0x20`. Teste `maple_main_device_only_at_own_port_address` |

Depois das correções: 130 testes unitários e `boot_trace` com disco real passam.

### 2026-10-14: correções do rasterizador (autosort, HALF_OFFSET, sprite, clamp). Relatório em `reviews/codex-20261014-105620.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Autosort usava o compare do polígono; o hardware força `>=` | **Aceito** | Manual 3.4.2. Corrigido; teste `render_autosort_forces_gequal` (translúcido atrás de opaco com ISP ALWAYS é rejeitado em autosort) |
| [MÉDIA] Region Array tipo 2 escolhe o modo por tile (bit 29) | **Limitação registrada + aviso** | O rasterizador não percorre o Region Array. Agora confere os tiles e avisa se algum diverge do modo global. Na captura do Flycast, os 300 tiles estão em autosort |
| [MÉDIA] Punch-through como z-buffer comum | **Aceito** | Manual 3.4.3. Ordenado por pixel da frente para trás, compare `>=`, o mais próximo que passa no alfa vence. Teste `render_punch_through_front_to_back` |
| [BAIXA] Testes não exercitavam UV do 4º vértice do sprite nem os bits 1 e 2 de HALF_OFFSET | **Aceito** | Testes `render_sprite_fourth_vertex_uv_perspective` (u(D) = 3/4 contra 0), `render_half_offset_attribute_sample`, `render_half_offset_texel_center` |
| [BAIXA] `fb_png.py` com limiar de 1 passo fixo (errado para 4444) | **Aceito** | Limiar por formato |

### 2026-10-14: revisão completa do `pvr_render.cpp` reconstruído. Relatório em `reviews/codex-20261014-102327.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] 4º vértice do sprite por A + C − B | **Aceito** | Manual 3.1.1.1 (equações de superfície). D tirado dos planos de z e de u·z, v·z. Testes de z e UV |
| [MÉDIA] HALF_OFFSET ignorado | **Aceito** | Bits 0-2 implementados. O jogo escreve 7 (o comportamento anterior). Os fixtures dos testes usam 7 |
| [BAIXA] Color clamp do TSP (bit 21) | **Aceito** | FPU_CLAMP_MAX/MIN aplicados com `min(max())`, sem UB se min > max. Teste `render_tsp_color_clamp` |
| [MÉDIA] Cobertura de testes insuficiente | **Aceito** | Testes por campo: modos de sombreamento, offset, ignore alpha, bilinear, clamp/flip, paleta 8 bpp, stride, 8+8 fatores de blend, punch-through, 8 compares, cor float e intensidade, sprite. O hash composto é só regressão |

### 2026-10-14: rasterizador, Maple, setjmp/longjmp entre threads, TMU. Relatório em `reviews/codex-20261014-093636.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] FB_W_CTRL com K e limiar de alfa trocados | **Aceito** | Layout conferido (K nos bits 15-8, limiar nos 23-16; mesma definição na referência de comportamento). Corrigido em `writeBack`. Teste `render_fb_0555_uses_k_from_bits_15_8` (com os campos trocados, falha) |
| [ALTA] Quadro reentrante intermediário com o mesmo R15 capturava longjmp de um quadro externo e abortava | **Aceito** | O despacho `bs_reenter_` só consome alvo da própria função; senão deixa o redirect pendente e retorna. Sem quadro de destino até o topo da thread, erro fatal (`fiberMain` e `bs-diff`). Teste `longjmp_skips_inner_reentrant_frame_with_same_sp` |
| [ALTA] Retomada só por R15 e thread nova inferida só por "alvo é entrada de função" | **Aceito** | O gerador emite a tabela de pontos de reentrada (`sh4_set_reentry_points`, 4 no jogo). Alvo de reentrada retoma a **única** thread suspensa com esse R15; nenhuma ou mais de uma é fatal. Thread nova só para alvo que não é reentrada. Teste `longjmp_to_reentry_without_suspended_thread_is_fatal` |
| [MÉDIA] Recorte do usuário (parâmetro tipo 1, PCW 17-16) ignorado | **Aceito** | Implementados os modos dentro/fora com o retângulo em tiles. Teste `render_user_clip_inside_and_outside` |
| [MÉDIA] DMA do Maple limitado a 8 descritores | **Aceito** | Segue até LAST; sem LAST em 1024 descritores, fatal. Teste `maple_follows_descriptors_until_last` |
| [BAIXA] Underflow do TMU com UNIE não gera interrupção | **Limitação registrada** | O jogo usa TCR0 = 2 (sem UNIE); o runtime avisa se algum canal ligar UNIE. Implementar quando aparecer |

Depois das correções: 91 testes unitários passam; `boot_trace` (3 000 chamadas, checkpoint `8C1C6334`, marco STARTRENDER) passa; o boot chega ao 150º STARTRENDER com 4 threads do guest e a tela "VMU not found".

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
