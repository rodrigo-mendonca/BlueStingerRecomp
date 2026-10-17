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
| **Port nativo** (`docs/PLANO-PORT-NATIVO.md` v2, aprovado 2026-10-15) | N0 concluído e aprovado (commit `e267149`). N1 (arquivos) concluído e aprovado (2026-10-15, commit único autorizado): ver "N1: sistema de arquivos nativo" e `docs/N1-RELATORIO.md`. N2 (tempo e eventos): plano aprovado como diretriz; **N2a concluído e aprovado** (2026-10-15, commit único autorizado): ver "N2a" e `docs/N2a-RELATORIO.md`. N2b: N2b-0/1 commitados (`18a5cc2`), investigação dos bloqueios (`246e6b3`); P0/P1 e **N2b-2 experimental** (`--tempo experimental`, padrão inalterado) sem commit: ver "N2b-2 experimental" e `docs/N2b2-RELATORIO.md`. |

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

### M4: executável nativo `bluestinger.exe` (2026-10-14, depois do commit `31a481e`)

Arquitetura: ARCHITECTURE.md, seção 6, "Executável nativo". Uso:
`bluestinger <imagem.cdi> [--flash f] [--map f] [--scale N] [--capture dir]`, ou
`--headless <vblanks> [--dump f.bmp]` sem janela.

**Boot nativo (só o CDI, sem snapshot nem BIOS)**, com o modo `BS_NATIVE_BOOT` do
`bs-diff`:
- trace de 3 000 chamadas **idêntico** ao do Flycast (as mesmas 3 diferenças aceitas);
- checkpoint `8C1C6334` (chamada 2 097 270): registradores, FPU e VRAM **iguais**. As
  508 236 palavras de RAM diferentes ficam **todas** em 0x0CDE0000-0x0D000000 (restos da
  rotina de boot do IP.BIN, não reproduzidos e não lidos pelo jogo até ali);
- sem a flash do Flycast (flash apagada), o jogo chega à tela "VMU not found" e, com
  START, ao título e ao menu.

**Mudança no poll central** (`holly_intc_sync` em `sh4_service`): sem ela, com SR.BL = 1
no boot, o VBlank do host nunca andava (`GameHost` travado). Depois dela: os 3 000
chamadas e o checkpoint continuam iguais; o 1º STARTRENDER continua no VBlank 18; o
`boot_trace` (que cobre as primeiras interrupções e o marco STARTRENDER) passa.

**Teste interativo real** (Release, janela SDL3, script em scratchpad): teclas
enviadas por `PostMessage` (WM_KEYDOWN/UP) **só para a janela do jogo**. Uma 1ª tentativa
com `SendInput` foi descartada: o Windows recusou dar o foco e a captura de tela pegou
outra janela. Capturas por `PrintWindow` e pela tecla F12 (`--capture`, quadro do
próprio jogo):
- tela "VMU not found" → ENTER (START) → abertura com o logotipo → "PRESS START BUTTON"
  com o texto de copyright → ENTER → menu **NEW GAME / LOAD GAME / OPTIONS** → setas
  movem a seleção (baixo: LOAD GAME; baixo, cima: LOAD GAME de novo);
- fechar a janela (WM_CLOSE): o processo encerra em 60-66 ms com código 0, nas três
  execuções, com 4 threads do guest em fibras;
- **XInput não testado fisicamente**: nenhum controle conectado nesta máquina
  (`XInputGetState` em 0-3 sem sucesso). O caminho XInput -> Maple só tem os testes
  unitários de serialização.

**Encerramento e vazamentos**: contador de fibras vivas (`sh4_live_fibers`). O teste
`game_host_stop_request_with_guest_thread` cria uma thread do guest numa fibra, para
pelo gancho de VBlank e confere o contador de volta ao valor inicial; um segundo
`GameHost` comeca do zero em outra thread. Erros (entrada que retorna, fatal do
guest) são relatados por `GameHost::error()`.

**Desempenho (Release, 16 núcleos lógicos)**. "Trabalho" = tempo de host por campo,
sem a espera do ritmo; orçamento = 16,68 ms:

| Situação | Resultado |
|---|---|
| Render da tela VMU / pós-START (2-304 triângulos) | 3-4 ms por render |
| Render do título 3D (~7 000 triângulos) | 9-11 ms por render |
| Cena do brilho na abertura do título (~2 milhões de fragmentos translúcidos, ~6,5 camadas de tela inteira) | antes: até 270 ms por render (geração 150 + resolução 70); depois: ~115 ms (geração ~89 + resolução ~26) |
| Execução headless até o título (2 400 VBlanks) | trabalho médio 10,5 ms, máximo 127 ms, 583 campos acima do orçamento |
| Janela, sessão até o menu (3 527 VBlanks) | média 13,2 ms, máximo 127 ms; 54,4 campos/s no último segundo (alvo 59,94) |

- As quedas se concentram no carregamento (VBlanks 0-300) e nas camadas translúcidas da
  abertura do título. No menu e no título parado, o jogo fica no ritmo.
- Otimizações feitas, todas com o **mesmo resultado bit a bit**: agrupamento por pixel
  (ordenação por contagem) no lugar da ordenação global, cache de texels por render
  (preenchido sob demanda) e resolução das linhas em paralelo. A geração de fragmentos
  continua sequencial.
- **Bug de fidelidade encontrado no caminho**: o render herdava o modo de ponto
  flutuante do SH-4 (arredondamento para zero + FTZ/DAZ que o runtime aplica na thread do
  jogo). Apareceu porque a resolução paralela, em threads com o modo padrão, dava outro
  resultado. Agora o render usa ambiente fixo (MXCSR 0x1F80). Teste
  `render_independent_of_guest_fp_mode`. Em relação ao render antigo, 34 de 913 920 bytes
  mudam num quadro do título, e duas execuções seguidas dão quadros idênticos.
- Os logs `[hle]`/`[gdrom]` a cada syscall passaram a ser opcionais (`BS_HLE_LOG=1`),
  porque pesavam no executável.

**Limitações**:
- sem áudio (AICA/ARM7 fora do M4);
- sem VMU: o jogo avisa "VMU not found" e não salva;
- abertura do título abaixo de 60 campos/s (rasterizador em software, geração
  sequencial). Próximo passo provável: gerar fragmentos por faixas de tela em paralelo,
  ou um renderer de GPU;
- dither e empates de texel continuam questões abertas (seção do oráculo gráfico);
- XInput sem teste físico;
- ainda não comparado com o Flycast no título e no menu (Prioridade 2 desta etapa).

**Depois da revisão do Codex (abaixo)**: 141 testes unitários e `boot_trace` passam;
trace nativo de 3 000 chamadas idêntico; checkpoint `8C1C6334` igual fora dos restos do
boot. Interativo repetido (Release): menu alcançado e navegado; encerramento em 52 ms,
código 0; sessão de 3 568 VBlanks com trabalho médio de 12,95 ms, máximo de 146,7 ms e
55,9 campos/s no último segundo.

### Distribuição Preview `dist/BlueStinger-Preview` (2026-10-14)

Montada por `tools/dist/make_preview.ps1` (Release; pasta temporária; troca só no fim,
com restauração da anterior se a troca falhar). Conteúdo: `BlueStinger.exe`, runtime do
MSVC app-local (`Microsoft.VC145.CRT` de `VCToolsRedistDir`: msvcp140*, vcruntime140*,
concrt140), `LEIA-ME.txt`, `VERSAO.txt` (commit `git describe` + data), `licencas/` (MIT,
SDL3 zlib). Nenhum dado do jogo; o executável contém código gerado da cópia do usuário,
por isso a pasta é local e não publicável (`dist/` no `.gitignore`).

Executável: subsistema gráfico (`/ENTRY:mainCRTStartup`), argumentos em UTF-16
(`CommandLineToArgvW`), diálogo de arquivo na inicialização sem imagem, imagem validada
(hash) antes de iniciar, `bluestinger.ini`/`bluestinger.log` ao lado do exe ou em
`%LOCALAPPDATA%\BlueStingerRecomp` se a pasta não for gravável, `--version`.

**Bug encontrado na montagem**: com o log redirecionado por `_wfreopen_s` (abre sem
compartilhamento, `_SH_SECURE`), reabrir `stdout` no mesmo arquivo falhava, o primeiro
`printf` disparava o handler de parâmetro inválido (0xC0000409) e o processo travava no
WerFault. Diagnóstico pelo trace `BS_APP_TRACE` e pelo handler de falha. Correção:
`_wfreopen` (compartilhado) para os dois fluxos.

Verificações:
- `tools/dist/test_preview.ps1` (fora do repositório: Preview copiada para
  `...\Prévia Ação ç 日本 (teste)\`, imagem por hard link em `Jogos ç ã 日本\`, diretório
  de trabalho = `%SystemRoot%`): **11 casos, 0 falhas**. Versão; headless 400 VBlanks com
  dump; imagem inválida e inexistente recusadas (código ≠ 0); janela abre, roda a
  59,9 campos/s e fecha com código 0; log ao lado do exe; ini lembra o caminho especial;
  diálogo sem imagem, cancelar sai com 0; pasta somente leitura: log **e** ini (UTF-8) em
  LOCALAPPDATA; imagem inválida na janela: mensagem e código 1.
- `tools/dist/test_make_preview.ps1`: **5 casos, 0 falhas**. Instalação nova; troca
  bloqueada (arquivo aberto na Preview anterior) sai com 1 e mantém a anterior; sobra
  `.old` de troca interrompida volta a ser a Preview; raiz de volume e repositório
  recusados como saída.
- Busca binária na montagem: nenhum caminho do repositório, do build ou do perfil do
  usuário nos `.exe`/`.dll` (ASCII e UTF-16); a montagem falha se aparecer.
- Suíte completa depois das correções: 141 unitários, `boot_trace`, `real_disc`,
  `codegen_exec_0/1` passam.
- **Não verificado**: Windows limpo sem Visual Studio/redistribuível instalado (a pasta
  leva o runtime do MSVC; a UCRT vem do Windows 10/11).

### Builder: CDI → `BlueStinger.exe` na máquina do usuário (2026-10-14)

Arquitetura: ARCHITECTURE.md, seção 6, "Builder" (D7). Escolhas do mantenedor: janela
Win32 nativa; kit de fontes + CMake/Ninja do Visual Studio do usuário.

**Fluxo real** (`bs-builder`, kit = repositório, saída nova em
`saída ç 日本 (builder)` numa pasta temporária):
- 4 038 funções, 12 arquivos, 980 666 linhas de C++ gerados em processo; 309 passos do
  Ninja (runtime + SDL3 + código gerado). Pacote verificado (`--version` e boot headless
  de 120 VBlanks) e instalado em **52-56 s** a partir do zero; incremental em **5-10 s**
  (só `main_sdl.cpp`, que leva a data da compilação, e a ligação).
- O exe instalado roda da própria pasta (`--version`; boot de 300 VBlanks). A janela do
  Builder foi conferida por captura (progresso, status, log, "Abrir pasta"/"Jogar").

**Problemas encontrados no caminho**:
- Carimbo do build gravado em modo texto (`\n` → `\r\n`): nunca batia e todo build era
  completo. Gravado em binário.
- Saída com 135 caracteres: objetos do SDL3 chegavam a 255-259 caracteres, e um
  `try_compile` do SDL (detecção de CPU) falhou com `MAX_PATH`.
- O Ninja do VS (1.12.1) usa `FindFirstFileExA`: `ç` (cabe na página 1252) funciona,
  `日本` vira `??` e o build incremental falha. O primeiro build passava porque nada tinha
  sido consultado ainda.
- Solução para os dois: tudo o que o Ninja vê vai para um cache curto e ANSI (build do
  CMake, C++ gerado e, quando preciso, uma cópia do kit). Limite de 200 caracteres para a
  saída, com mensagem clara.

**Testes**:
- `tests/unit/test_builder.cpp` (6 casos, sem compilador nem dados do jogo):
  - aspas de linha de comando (ida e volta por `CommandLineToArgvW`);
  - página ANSI; processos filhos: código, UTF-16, ambiente, inexistente, e cancelamento e
    tempo limite que matam o neto (`ping`), conferido por lista de processos;
  - `install_swap`: preserva ini/saves, rollback com arquivo em uso, recupera sobra
    `.old-*`;
  - entradas inválidas sem efeito colateral.
- `tools/dist/test_builder.ps1` (distribuição copiada para `Builder ç 日本 (teste)`, CDI por
  hard link em `Jogos ç ã 日本`): 15 casos.
  - Sucesso: compilação completa a partir de saída nova (kit copiado para o cache);
    exe responde e dá boot; segunda saída nova com C++ **idêntico** (reprodutível);
    incremental (2 passos).
  - Recusas: CDI inválido; sem compilador (mensagem de instalação); saída sem permissão;
    saída longa demais; sem kit.
  - Falhas sem destruir nada: falha de compilação (kit com erro de sintaxe) → código 1,
    instalada intacta, sem pacote temporário; jogo aberto → troca recusada; cancelar pela
    janela → código 3, instalada intacta.
  - Janela até o fim com o ini do usuário preservado.
  - 1ª execução: 14 ok. O caso "sem kit" falhou só porque o teste comparava com o hash de
    antes do caso 11, que instalou uma versão nova (o Builder recusou corretamente).
    Teste corrigido.
- **Depois da revisão do Codex (abaixo)**: 8 testes unitários do Builder (149 no total, todos
  passam); `test_builder.ps1` **15 casos, 0 falhas**. Builds completos de 51 s e
  incremental de 5 s (2 passos). `ctest` completo passa (`boot_trace`, `unit`,
  `real_disc`, `codegen_exec_0/1`). Preview remontada: `test_make_preview` 5/5;
  `test_preview` 11/11 em 4 de 5 execuções. **Uma execução teve 2 falhas não
  identificadas**: só a última linha foi capturada, e elas não se repetiram em 4 execuções
  seguidas. O teste agora grava `resultado.txt` na pasta de teste para diagnosticar se
  voltar.

### Independência do CDI (2026-10-14, depois do commit `a5a1fe4`)

Exigência: o CDI só serve para a conversão. Arquitetura: ARCHITECTURE.md, "Independência do
CDI".

- **Preparação** (`bs-extract --runtime`, e o Builder): 2 092 arquivos (708 MB) e 71
  setores de sistema (0,1 MB) em 8,2 s.
- **Equivalência setor a setor**: todos os ~360 mil setores das duas faixas de dados são
  idênticos à imagem (1,3 s).
- **Mesma imagem na tela**: o quadro do VBlank 2 000 é idêntico (mesmo hash) lendo o CDI
  (Preview anterior) e lendo `data\` (executável novo).
- **Teste `runtime_data`** (CDI real, ctest):
  - prepara e confere todos os setores e o SHA-256 dos arquivos;
  - `boot_read_data` é igual a `boot_read_disc`;
  - faixas e setores da fonte do GD-ROM são iguais aos do CDI;
  - um arquivo alterado é detectado.
- **Unitários** (`test_runtime_data.cpp`, sem dados do jogo):
  - composição dos setores (meta > arquivo > zeros; faixa de áudio; fora das faixas);
  - último setor completado com zeros;
  - mensagens claras: dados ausentes, arquivo ausente, tamanho diferente, formato,
    caminho perigoso (`..`, nomes de dispositivo do Windows), arquivo apagado com o jogo
    rodando;
  - mods de arquivo.
- **`test_builder.ps1`** (casos novos):
  - jogo instalado dá boot com o **CDI renomeado** (inacessível);
  - instalação **movida** para outra pasta (`movido ç 日本`): boot e `--verify-data`;
  - nenhum caminho do CDI, do repositório ou do perfil do usuário nos arquivos instalados;
  - arquivo de dados ausente: mensagem com o nome do arquivo e o Builder.
- **`test_preview.ps1`** (9 casos):
  - Preview gerada pelo próprio Builder;
  - `--verify-data`;
  - headless sem CDI em caminho especial;
  - CDI na linha de comando: recusado com explicação;
  - janela abre e fecha;
  - sem `data\`: mensagem no headless e caixa de mensagem com código 1 na janela;
  - pasta somente leitura.

### NEW GAME e primeira área jogável (2026-10-14)

**Causa do erro relatado** (`desvio indireto ... PC=8C1953E2 (valor 8C194B80)`):
- `8C1953E2` é `jsr @r3` com `r3 = @(r0,r11)`: método de objeto, entrada 1 de uma tabela
  de métodos.
- Reproduzido headless (roteiro de entrada) com o valor `8C194580`. O valor vem de memória
  e muda entre execuções. `8C194B80` cai no meio da função `8C194B6C`, depois do prólogo.
- `8C194580` é `rts; mov #0,r0`, referenciado só pela vtable em `8C1DBF58` (logo depois do
  texto de copyright da CRI). A vtable tem 8 métodos curtos desconhecidos e 5 conhecidos.
- O analisador recusava folhas de 2 instruções vindas de ponteiro em dado.

**Correção geral**: regra de tabelas de ponteiros (ARCHITECTURE.md). Resultado: +45
funções (44 stubs de 2 instruções), 0 fins mortos, 0 órfãs, nenhuma removida; amostras
conferidas no disassembly.

**Testes**:
- `analysis_pointer_table_finds_short_methods`: sintético, com caso negativo (1 conhecida
  só). **Sensibilidade comprovada**: sem a regra, o teste falha.
- `analysis_real`: binário real; toda a vtable `8C1DBF58..88` e `8C194580` com origem
  `pointer_table`; 0 fins mortos/órfãs.

**Comparação com o Flycast**: não feita nesta etapa. O oráculo não recebe entrada de
controle (STATUS, controles), então não chega ao NEW GAME. A divergência foi resolvida por
análise estática e pelo disassembly do binário original.

**Bloqueios seguintes e correções**:
1. `conversor YUV do TA ainda nao implementado` (PC `8C1C1E38`, escrita em `0x10800000`):
   conversor YUV implementado (ARCHITECTURE.md). Teste `ta_yuv_converter_macroblocks_to_vram`
   (420 em textura única, 422 em texturas 16x16, contador, interrupção, reinício).
2. Personagem e objetos sem textura: TCW `480670C0`/`500687D0` (VQ 565/4444). VQ e mipmap
   implementados. Testes `texture_vq_and_mipmap_offsets`, `texture_yuv422_linear`. Os
   hashes antigos do rasterizador continuam iguais.

**Partida real** (Preview sem CDI, roteiro versionado `tools/dist/roteiros/newgame_area1.txt`):
- **Sequência**: VMU, Activision, Climax, título, menu, NEW GAME, vídeo (curto, ver
  abaixo), prólogo em tempo real (porto, helicóptero, explosão, Eliot caído, a Nephilim),
  depois a **primeira área jogável** (cais da Dinosaur Island, portão C-GATE, HUD
  "ELIOT" com a barra de vida) a partir do VBlank ~5 400.
- **Controle**:
  - frente/esquerda/direita/trás movem o Eliot, e a câmera acompanha (atrás dele, afastando
    ao andar);
  - A (ação) não trava nada; o efeito dele nesta área não foi identificado;
  - 7 300 VBlanks sem erro.
- **`test_gameplay.ps1`** (4 casos):
  - 7 300 VBlanks sem erro e sem CDI;
  - barra de vida do HUD (557 pixels verdes);
  - o movimento muda a cena (diferenças 152 e 142);
  - **sem controle, a cena fica parada** (3,8): o movimento vem da entrada.

**Em aberto**:
- **Vídeo NEWGAME.SFD** (193 MB): mostra cinza com faixas horizontais por ~150 campos e o
  jogo segue para o prólogo. O Sofdec lê só ~15 blocos (~0,8 MB) do arquivo e para.
  - Croma exatamente 128 em todos os macroblocos e luma com só variação vertical.
  - Os setores lidos são idênticos à imagem.
  - **Hipótese, não verificada**: o player sincroniza pelo relógio do áudio (ADX no AICA,
    driver MANATEE.DRV no ARM7, marco M6) e aborta sem ele.
  - Os semânticos de FPU usados pelo IDCT (`fschg`/`fmov` 64 bits/`ftrv`) foram conferidos
    contra o manual.
- Sem áudio e sem VMU (inalterado). A partida sem som é jogável.

### Partida longa e investigação do NEWGAME.SFD (2026-10-14, noite)

**Exploração longa** (Preview sem CDI, roteiro versionado
`tools/dist/roteiros/newgame_exploracao.txt`, 20 000 leituras de controle, captura a cada
100 VBlanks):
- **20 000 VBlanks** (~5,5 min de jogo) **sem erro**, código 0.
- Trabalho médio de 29,9 ms por campo (~35 campos/s, abaixo do tempo real; desempenho do
  rasterizador continua pendente). Pico de 1,23 s (provável carregamento).
- **Movimento e câmera**: o Eliot percorre o cais para frente, para os lados e para trás, e
  chega à ponta do cais diante do mar. A câmera acompanha atrás dele.
- **Ociosidade**: parado, ele faz animações de espera (coça a cabeça, se alonga). A lógica
  do jogo continua rodando.
- **Não confirmado**:
  - A, B, X e Y nesses pontos não mostraram efeito visível (pode não haver o que interagir
    ali);
  - em 6 900-7 800 ele corre no lugar, aparentemente contra um obstáculo.
  - Sem comparação com o oráculo (que não recebe entrada), não dá para dizer se algum
    desses dois casos é divergência.

**Carregamentos e ritmo** (mesma exploração com `BS_FIELD_LOG=1`):
- **Carregamentos**: não aparecem como picos. O GD-ROM devolve na hora, e nenhum campo
  passou de 160 ms. O pico de 1,23 s da 1ª execução não se repetiu (provável soluço do
  host).
- **Ritmo**: o jogo renderiza **um campo a cada dois** (30 quadros/s). Na área jogável, cada
  campo com render leva ~45-55 ms (média 44-59 ms por faixa de 2 000 VBlanks), o que dá
  ~17 quadros/s contra 30. O gargalo é o rasterizador por software.
- **Picos de 100-160 ms**: só nas cenas com muitas camadas translúcidas, que são o brilho
  do título (VBlank 1 233-1 325) e o fogo/fumaça da explosão no prólogo (2 872-2 896 e
  3 136-3 208).

**NEWGAME.SFD: fatos comprovados**:
1. Só existem os comandos 16/17 (leitura PIO/DMA) do GD-ROM. Os setores lidos do SFD são
   idênticos à imagem (equivalência setor a setor).
2. O jogo lê `BSDATA/MANATEE.DRV` (driver do ARM7) para 0x0CE80500 e `BS_SND.MLT` para
   0x0CCD9D00.
3. `BS_AICA_TRACE`: a CPU escreve 457 753 palavras não "SEGA" na RAM de som, ou seja, o
   driver e os dados são copiados pela CPU.
4. `BS_SB_LOG`: o DMA do G2 (`SB_AD*`) só é configurado (valores de inicialização do
   Katana), nunca disparado (`SB_ADST` = 0).
5. A área de trabalho do driver (0x00813400 com 2 280 leituras, cerca de uma por campo;
   0x00813A00/18 com 253 cada; 0x008138xx) é lida por uma rotina genérica de leitura da RAM
   de som (PR = `8C08EC4A`). Ela **nunca muda** (preenchimento "SEGA"), porque o ARM7 não
   roda.
6. Oráculo (comando novo `mem` do `flycast_oracle.py`): o stub GDB do Flycast lê a RAM de
   som (controle: `EAFFFFFE` em 0x00800000).
   - Na 50ª e na 1 500ª passagem por `8C08EC4A`, o Flycast tem a RAM de som ainda com o
     stub do ARM (`EAFFFFFE`) e a área do driver zerada.
   - O nosso, na 50ª leitura de 0x00813400, já tem o vetor do driver (`EA00003E...`) e
     "SEGA" na área.
   - **Os pontos não estão alinhados**: o contador de passagens não mede a mesma coisa nos
     dois, porque no Flycast a rotina roda muitas vezes antes de o driver ser copiado.
     **Ainda não é a primeira divergência comprovada.**

**Conclusão parcial**:
- A dependência do driver de som (ARM7 + AICA) está comprovada na inicialização do som.
- A ligação com o fim precoce do vídeo **não** está comprovada.
- Nenhuma correção foi implementada (seria fictícia sem o ARM7: fingir respostas do
  driver).

7. Âncora tentada: "primeira passagem pela rotina de escrita na RAM de som (retorno
   `8C08EF96`, onde o nosso grava o 1º vetor do driver, ciclo 58,8 M)" com o novo
   `--depois` do `mem`. Não serve: a mesma rotina também faz o preenchimento "SEGA" antes.
   - No Flycast, na 1ª e na 500ª leitura depois dela, a base da RAM de som ainda é o stub e
     a área do driver está **zerada** (não "SEGA"), ou seja, antes do preenchimento.
   - Uma âncora precisa de condição (endereço + valor escrito), que o stub GDB não oferece
     diretamente.
   - O `flycast_oracle.py` teve um `ConnectionResetError` intermitente ao abrir sessões
     seguidas (resultado descartado).

8. **Âncora válida e primeira divergência comprovada**:
   - O reset do ARM7, a cópia do driver e a liberação do ARM7 (escrita 0x300 em
     0x00702C00, ciclo 58,99 M) acontecem dentro de uma rotina de inicialização de som que
     retorna para `8C02A288`. Achada pela pilha do SH-4 no momento da escrita (diagnóstico
     novo).
   - A 1ª passagem por `8C02A288` é a âncora (o stub GDB não aceita watchpoints `Z2`/`Z1`,
     resposta vazia).
   - Controle: nessa âncora, a base da RAM de som é **igual** nos dois (`EA00003E EA00008A
     EA000089 EA000088`, os vetores do driver).
   - **Divergência**: a área do driver em 0x00813400:

     | | Na âncora | 300 leituras depois |
     |---|---|---|
     | Flycast | `00000000 00000000 00018000 00000000 / 0000001A ...` | `00000000 00000000 00018000 00000000 / 0000001E 00000000 0000001E 00000000` |
     | Nosso | "SEGA" (41474553) em todas as palavras | "SEGA" em todas as palavras, e assim fica até o VBlank 2 300 |

     No Flycast o driver já inicializou a área e um contador anda; 0x00813A00 está zerada
     lá e com "SEGA" aqui.
   - **Causa**: o ARM7 não executa o MANATEE.DRV. O driver é copiado corretamente (vetores
     iguais), mas nada roda nele.
   - **Correção**: só com a emulação do ARM7 e do AICA (plano em ARCHITECTURE.md, aguarda
     aprovação). Simular respostas do driver seria uma solução fictícia, e não foi feito.
   - A ligação com o fim do vídeo continua **não comprovada**. Este é o primeiro ponto
     divergente do subsistema de som.
   - `flycast_oracle.py`: o `ConnectionResetError` intermitente aparece depois de a leitura
     já ter sido impressa (fim da sessão), então não afeta os valores. A conexão agora tenta
     de novo em caso de reset/recusa.

**Próximo passo**:
- Com o ARM7 rodando, conferir que a área do driver fica igual à do oráculo nessa âncora.
- Então ver se o vídeo vai até o fim.
- Depois, localizar no Sofdec a decisão que encerra o vídeo, com uma pilha de chamadas
  sombra (o observador `BS_FN_ENTER` só vê entradas).

Plano de áudio (AICA, ARM7, Manatee): ARCHITECTURE.md, "Plano de áudio nativo" (proposta,
aguarda aprovação).

### Mods e projeto C++ editável (2026-10-14)

Arquitetura: ARCHITECTURE.md, "Mods e projeto C++ editável".

- **Unitários**:
  - `mods_replace_direct_and_indirect_calls`: slot (direta), despacho (indireta),
    `call_original`, `reset`, registro e `init_all` do `BS_MOD`;
  - `runtime_data_asset_overrides`: mesmo tamanho, menor (zeros), o último setor vem do
    mod e não da meta, maior recusado.
  - Os testes de codegen agora esperam `BSG_FN(...)`.
- **Fluxo real** (CLI):
  - `--projeto` exportou o projeto com `mapa_funcoes.tsv` (4 083 funções). Exemplo:
    `0x8C194580 → f_8C194580 → funcs_008.cpp → pointer_table → 2`.
  - Com o exemplo de mod e uma alteração controlada no C++ gerado (marcador em
    `f_8C010000`), `--de-projeto` recompilou **sem o CDI** em 20 s (incremental).
  - O jogo instalado mostrou `[mods] iniciando exemplo_contador`,
    `[edicao controlada] entrada do jogo` e `[mod exemplo] primeira chamada de 8C194580`.
    A substituição pegou justamente a função do erro do NEW GAME, chamada por ponteiro.
- **`test_projeto.ps1`** (9 casos, distribuição do Builder; os 2 últimos acrescentados na noite de 2026-10-14):
  - exporta;
  - recompila do projeto com o CDI inacessível;
  - mod em C++ ativo;
  - alteração no gerado ativa;
  - mod de arquivo carregado: cópia idêntica, porque alterar `CNF00.NT` (arquivo de
    configuração) muda o comportamento do jogo e a 1ª tentativa do teste falhou por isso;
  - mod de arquivo grande demais: mensagem clara;
  - reexportar preserva `mods/` e guarda o `generated/` editado;
  - mod removido: o efeito some depois de recompilar (o cache espelha `mods/`);
  - reexportar mantém uma edição mais nova em `runtime/`.
- Unitário `mods_replace_order_restore_and_no_slots`: substituições seguidas (vale a
  última), `replace(addr, nullptr)` restaura, build sem slots só muda as indiretas.

### Revisão do Codex nesta etapa

**Não realizada**: `codex exec` retornou "You've hit your usage limit ... try again at
11:14 PM". O script `codex_review.ps1` agora mostra o motivo da falha. No lugar, revisão
própria dos pontos pedidos:
- reconstrução de setores (sobreposição, último setor, mods);
- caminhos vindos do disco/manifesto: acrescentada a recusa de nomes de dispositivo do
  Windows, com teste;
- regra de tabelas de ponteiros (caso negativo testado);
- slots de mods (chamadas diretas, tail calls, tabelas locais, reentrada);
- hard links de dados: arquivos nunca reescritos no lugar; o Builder confere o SHA-256
  antes de reaproveitar.

**Pendente**: repetir a revisão do Codex quando o limite liberar.

**Achados da verificação final**:
- **Rollback** (`.anterior`): ocupada por uma pasta sem a marca do Builder (a Preview antiga,
  montada pelo script anterior), a versão anterior ficava como `.old-*` e seria apagada na
  execução seguinte. Agora ela vai para `<destino>.anterior-<data>`, e nada sem a marca é
  apagado. Teste no `builder_install_swap_recovery_and_cancel`. Em `dist/`, as pastas
  foram só renomeadas: `BlueStinger-Preview.legado-cdi` (Preview que lia o CDI, 16:15) e
  `BlueStinger-Preview.anterior` (20:17).
- **Intermitente, não reproduzida**: uma execução do `test_builder` (caso "jogo aberto")
  parou em "verificando o executavel": `--version` com código 0 e sem a linha de versão.
  O resultado foi seguro (erro, instalada intacta). Mais tarde:
  - o cenário isolado passou 3/3;
  - o teste de estresse `builder_run_process_short_lived_output_is_never_lost` passou: 300
    processos curtos por execução, 3 execuções em repouso e 3 com dois jogos rodando em
    paralelo, sem nenhuma saída perdida.
  - A verificação agora tenta `--version` duas vezes, registra no log quando a 1ª falha e
    põe a saída capturada na mensagem.
  - Depois disso, `test_builder` 18/18.
- **Totais**: 159 testes unitários; `ctest` 7/7 (com `runtime_data` e `analysis_real` no
  CDI real); `test_builder` 18/18; `test_preview` 9/9; `test_gameplay` 4/4;
  `test_projeto` 7/7.

### N0: fundação do port nativo (2026-10-15)

Autorização do mantenedor: plano v2 aprovado, **só o N0**. Commit autorizado e feito antes:
`c1ab476`. O N0 está sem commit. Números, método e reprodução estão em
`docs/N0-RELATORIO.md`.

**Verificado (método):**
- **Manifesto de hardware** (`bs-hwscan`, propagação de constantes por função sobre a
  análise do tradutor):
  - 625 referências estáticas (235 acessos, 390 constantes), **0 em funções do jogo**;
  - sonda dinâmica (`BS_HW_PROBE`, roteiro NEW GAME, 8 000 VBlanks, build release):
    1 592 acessos distintos, **0 do jogo**;
  - falsos positivos conferidos no disassembly: `0xFF000000`/`0x80704820` como cor em
    `8C0623C0`; `0xE0000000` guardado numa estrutura em `8C058720`.
- **Identificação do SDK:**
  - camada para 4 000 de 4 083 funções: jogo 1 365, SDK 2 635;
  - família com evidência e confiança para 1 100: CRI 465, Shinobi 298, `sd` 82, Kamui 79,
    Ninja 69, runtime C 23, BIOS 16, jogo por nome 68;
  - 46 banners de módulo;
  - assinaturas conferidas no disassembly: `__sdivsi3` (`8C08CF5C`), `rand` (`8C08D5D0`),
    `njSin`/`njCos` com ângulo de 16 bits (`8C16FE00`/`8C16FE48`), funções de matriz do
    Ninja (`8C1713A4`, `8C170DC8`).
- **Fronteira e fechamento:**
  - a fronteira declarada (`config/fronteiras_us.toml`) tem 267 entradas: 250 diretas e
    17 indiretas, 45 observadas pela sonda;
  - fechamento com 0 violações, estático e com a sonda;
  - sem as arestas observadas sobravam 8 alvos indiretos (matemática/runtime C chamados por
    ponteiro), e por isso a fronteira declarada junta os dois.
- **Métricas:**
  - o código gerado reproduz a seção 1.1 do plano: `BS_POLL` 2 287, `sh4_dispatch` 2 601,
    FPU 71 999, `BS_CALL` 14 987;
  - a catraca está em `config/metricas_base_us.toml`.
- **Custo dos slots** (`BS_MODS` em todas as chamadas), alternado, 4 000 VBlanks: normal
  20,20/20,09 ms por campo; slots 20,13/20,04. Sem diferença mensurável.
- **Ordem de eventos** (`BS_EVENT_TRACE`, `tools/dist/test_ordem.ps1`, 3 000 VBlanks):
  - duas execuções com trace idêntico (8/8);
  - IRL 0x320 ×9 002, 0x360 ×2 982;
  - 3 threads do guest, 14 880 trocas, 11 902 longjmp, rotina adiada `8C17DE4C` ×2 535.
- **CDI fora do produto:** o alvo `bs_runtime_cdi` saiu do runtime. `n0_produto_sem_cdi`
  confere o mapa do linker, e o caso negativo foi conferido à mão (mapa falso com `CdiImage`
  → falha). A Preview remontada também não tem `CdiImage`.
- **Comportamento inalterado, provado por diferencial determinístico:**
  - build anterior ao N0 (`c1ab476`, worktree separada, mesmo `generated/`) × build final,
    com o roteiro NEW GAME e dados de execução novos (`bs-extract --runtime`, conferidos setor
    a setor);
  - quadro final depois de exatamente N VBlanks (`--headless N --dump`), duas execuções de
    cada;
  - **idêntico bit a bit** em N = 1 500 / 3 000 / 4 500 / 6 000 (SHA-256 7C63431E…,
    32CBE992…, 29508FDD…, B49D68E2…).
  - Observação de método: `--dump-every` amostra de forma assíncrona (thread principal com
    `Sleep(1)`). O mesmo build contra ele mesmo já difere nos nomes e em 1 quadro, então não
    serve como medida. Comparando por nome, antes × depois deu 43 iguais e 0 diferentes.
  - O trace de ordem do build final tem o mesmo SHA-256 de antes das correções da revisão
    (`5047B269…`).
- **Store queues** (`analysis/hwscan/sq_provavel.tsv`):
  - as 260 funções com `pref` não resolvido e literal `0xE0000000` são todas do SDK, nenhuma
    do jogo; 41 enviaram por SQ no roteiro;
  - a sonda achou mais 12 funções com SQ que montam o endereço sem o literal (SDK ou sem
    camada);
  - destinos: FIFO do TA (26,1 M envios) e RAM (34,0 M). O jogo não usou SQ.
- **Reprodutibilidade:** regenerar `config/fronteiras_us.toml` (com a sonda) e
  `config/metricas_base_us.toml` dá arquivos idênticos byte a byte aos versionados: 267
  entradas, 45 observadas.
- **Risco mantido:** 1 443 alvos indiretos não resolvidos (`fechamento.indireto_nao_resolvido`,
  com catraca). **Não** contam como cobertura comprovada.

**Testes:**
- 182 unitários (22 novos do N0, 5 deles da revisão);
- `ctest` 11/11, com `real_disc`, `runtime_data` e `analysis_real` no CDI real e os novos
  `n0_native_api`, `n0_native_limpo`, `n0_fechamento` (com caso negativo) e
  `n0_produto_sem_cdi`.
- Suítes de distribuição: rodadas duas vezes, antes e depois das correções da revisão. Na rodada final (código final,
2026-10-15 08:19):
  - `make_builder` e `make_preview` (Preview remontada pelo Builder com o kit novo) ok;
  - `test_preview` 9/9; `test_gameplay` 4/4; `test_builder` 18/18;
  - `test_projeto` 9/9: projeto editável, recompilar sem o CDI, mod C++, edição do gerado,
    mod de arquivo, mod grande demais, remoção, reexportação.

**Revisão do Codex:** Feita em 2026-10-15 (`reviews/codex-20261015-020129.md`, somente leitura, sem rodar
build). Seis achados, **todos aceitos**, cada um com teste que o reproduz:

| Gravidade | Achado | Decisão e correção |
|---|---|---|
| ALTA | O fechamento excluía as 1 615 funções do SDK com endereço só em dado; "0 violações" não as provava | Aceito. Elas viram `indireto_nao_resolvido` em `fechamento.tsv`: são alvos possíveis de chamada indireta do jogo, sem prova. A métrica `fechamento.indireto_nao_resolvido` (1 443 fora da fronteira) entrou na catraca, e o relatório foi corrigido ("nenhum caminho provado; fechamento ainda não completo"). Teste `n0_closure_reports_data_only_indirect_targets`. |
| MÉDIA | `pref` de endereço calculado não entrava no alcance de hardware: entradas com SQ caíam em "sem hw" | Aceito. Função com `pref` não resolvido e literal `0xE0000000` conta como render (SQ provável: 260 funções, métrica `pref.sq_provavel.funcoes`). Teste `n0_unresolved_pref_with_sq_literal_reaches_render`. |
| MÉDIA | `fmov` de memória fixado em 4 bytes, mas FPSCR.SZ=1 transfere 8 e incrementa 8 (manual SH-4; `sh4_ops.h`) | Aceito. O acesso é registrado com 8 bytes (pré-decremento em base-8) e o pós/pré-incremento invalida a base. Teste `n0_scan_fmov_size_depends_on_fpscr_sz`. |
| MÉDIA | Voltar ao legado apagava um mod do usuário instalado antes | Aceito. O registro guarda o provedor anterior e o restaura; um mod instalado durante o nativo fica. Novo `sh4_lookup_generated` (aditivo). Teste `transitional_boundary_registry_preserves_user_mod`. |
| MÉDIA | Depois de 4 095 gerações o handle antigo voltava a valer | Aceito. O índice com a geração esgotada é aposentado. Teste `transitional_handle_table_retires_exhausted_index`. |
| MÉDIA | `IClock` em 60 Hz inteiro, mas o campo NTSC é 1001/60000 s (`game_host.cpp`, `pvr_regs.cpp`) | Aceito. `time::Rate` racional (60000/1001) com `frame_ns` exato: 60 000 campos = 1 001 s (`static_assert` em `n0_native_api`). |

Pontos que o Codex conferiu sem problema:
- delay slot antes do clobber de chamada;
- `MOVW_PC` com sinal; `OR`/`AND #imm` só em r0;
- deslocamentos por tamanho; `mov.l @r1+,r1`;
- **nenhum `MOV*_GBR` no binário** (a falta de GBR no scanner não deixa falso negativo nesta
  imagem);
- `cur_fn` em chamadas, interrupções e fibras; reset do contador de VBlank;
- `ram_offset` só área 3; separação do CDI no mapa;
- interfaces viáveis com D3D11/XAudio2/Media Foundation.

Não verificado pelo Codex (registrado no relatório):
- a premissa "SDK não chama jogo diretamente, exceto a partida", fora das arestas
  identificadas;
- cobertura além da 1ª área.

Depois das correções: 182 unitários, `ctest` 11/11. A fronteira e a base foram regeradas.

### N1: sistema de arquivos nativo (2026-10-15)

Autorização do mantenedor: N1 dentro do escopo de `docs/N1-PLANO.md`, sem N2 e sem commits
adicionais. Relatório: `docs/N1-RELATORIO.md`.

**Verificado (método):**
- **Identificação por contrato da API gdFs (Shinobi 1.05).**
  - Método: disassembly de `0x8C15A5E8`-`0x8C15C300`; trace de argumentos e retornos
    (`BS_CALL_TRACE`, novo, build BS_MODS, NEW GAME 6 000 VBlanks, 68 aberturas).
  - Estrutura GDFS deduzida.
  - 21 endereços que criam ou recebem GDFS: 17 com código gerado, 4 nunca alcançados. Mais o
    servidor `8C15AE28`.
- **Slots sempre ativos** (`[native] slots`, gerador `BSG_SLOT`): comportamento idêntico, com
  o quadro em 6 000 VBlanks igual ao do N0 (`b49d68e2…`).
- **Provedor nativo** (`DataFileSystem` + adaptador gdFs, padrão), roteiro NEW GAME de 8 000
  VBlanks:
  - 68 aberturas, 108 leituras, 15,8 MB, 0 não encontrados;
  - **0 setores de arquivo pelo GD-ROM HLE** (P2);
  - sequência global de 7 750 blocos `(arquivo, deslocamento, SHA-256)` **idêntica** ao legado
    (P1), comparando os 88 grupos de arquivos que dividem setores no disco pelo grupo;
  - determinístico: log e quadro iguais em duas execuções;
  - eventos +2 VBlanks depois da tela-título (tempo de carga), por isso o quadro num VBlank
    fixo difere (investigação no relatório, seção 5).
- **Rollback:** `--arquivos legado` dá o quadro do N0 em 6 000 VBlanks (`b49d68e2…`, P8).
- **Fronteira:**
  - 5 entradas gdFs com `provedor = "nativo"`;
  - `fronteira.provedor.legado` 267 → 262;
  - `legado.subsistema.arquivos` 17 → 2: inicialização `8C08BB40` e estado do drive
    `8C15C016`, ainda legados.
- **Investigação da diferença inicial (`IWP20` × `IWP15`).**
  - Descartados por experimento: tempo de carga (0-100 000 ciclos por setor não mudaram o
    arquivo), sobras após o fim do arquivo (nenhuma no disco) e valor do handle.
  - Causa provada: `IWP15.NT` e `IWP20.NT` dividem LBA, tamanho e SHA-256, e o log do legado
    (setor → arquivo) não distingue aliases. O trace de chamadas e os snapshots de RAM
    (`BS_RAM_DUMP`, novo) mostram `IWP15` nos dois modos.
- **Regressão encontrada e corrigida:** o projeto exportado traz `mods/assets/LEIA-ME.txt`, que o
  provedor nativo recusava como "mod sem recurso", e o Builder falhava em "verificando o
  executável". Agora a documentação na raiz da pasta de mods é ignorada (teste
  `n1_data_files_mods_readme_ignored`). O texto do projeto exportado passou a refletir as
  regras de mods do N1.

**Testes:**
- 192 unitários (10 do N1);
- `ctest` 11/11 com o CDI real;
- suítes de distribuição (2026-10-15, `make_builder` e `make_preview` refeitos, depois cada
  suíte): `test_preview` 9/9, `test_gameplay` 4/4, `test_builder` 18/18, `test_projeto` 11/11,
  `test_arquivos` 6/6 (P1 7 750/7 750 blocos em 54 arquivos, ordem global igual; P2 0 setores;
  P3/P8 roteiro completo; 68 aberturas nativas, 0 não encontrados), `test_ordem` 8/8 (ordem
  `176F4C28…` igual em duas execuções).

**Revisão do Codex** (`reviews/codex-20261015-101006.md`, somente leitura):

| Gravidade | Achado | Decisão |
|---|---|---|
| ALTA | `ReqRd32` concluía na chamada; o binário publica "ocupado" (2) com 0 bytes (`8C15B966`/`8C15B978`) e conclui no servidor | **Aceito.** Pedido pendente (um por vez), concluído no gancho do servidor `8C15AE28`; `Stop`/`Close` cancelam; leitura síncrona conclui o pendente antes. Teste atualizado. Medido: não mudou leituras nem quadro do roteiro. |
| MÉDIA | P1 passava com conjunto de arquivos diferente | **Aceito.** P1 também exige o mesmo conjunto e a mesma contagem de blocos, comparando por grupo de conteúdo. |
| BAIXA | A explicação por tempo não isolava a causa | **Aceito.** Os experimentos refutaram o tempo; a causa real foi provada (aliases no log do legado). |
| BAIXA | Tamanho maior que 32 bits truncado no contrato gdFs | **Aceito.** Mod acima de 4 GiB − 1 recusado na carga e na validação, com o caminho (`check_sizes`); abertura recusada. |
| BAIXA | Erros ao percorrer a pasta base eram descartados | **Aceito.** Abortam com caminho e motivo. |
| BAIXA | `emitFunction` dependia de um global | **Aceito.** `FnEmitter` usa `opt_.always_slot`; teste `n1_codegen_always_slot_in_emit_function`. |
| (não verificado) | Mod ilegível sem teste | **Coberto:** `n1_data_files_unreadable_mod` (arquivo com acesso exclusivo). |

**Depois do commit `770e3d1` (2026-10-15).** No build local `msvc-release`, o executável
terminava com `0xC0000409` ao sair, nos dois provedores de arquivos. Diagnóstico:
- log de eventos do Windows: `__fastfail(2)`, cookie de pilha;
- `ninja -t deps`: `main_sdl.cpp.obj` era das 10:23, de antes do membro `ram_` entrar em
  `game_host.h`. O `main` montava um `GameHost` menor que o real.

Causa:
- o ninja não registrou dependências de cabeçalho para 110 de 375 objetos;
- o prefixo do `/showIncludes` foi detectado em português ("Observação: incluindo arquivo:") e
  parte das compilações devolve o texto em outra codificação.

O código está correto: a Preview e o Builder compilam do zero e passaram em todas as suítes.
Com build limpo (debug e release), `ctest` deu 11/11 e o executável sai com 0 nos dois modos.
Ainda há 31 objetos sem dependências registradas. Até a correção (decisão D6 em
`docs/N2-PLANO.md`), cada verificação usa build limpo.

### N2: planejamento (2026-10-15)

Autorização do mantenedor: só o plano, sem implementação. O plano está em `docs/N2-PLANO.md`.

**Levantamento feito para o plano** (só diagnóstico, nenhuma mudança de código):
- Trace de eventos e de leituras, 300 VBlanks, nos dois provedores de arquivos. Até V40 as
  sequências diferem; de V41 a V220 são iguais; a transição para a tela-título cai em V221
  (legado) × V223 (nativo).
- A causa ainda não está provada: o método está na seção 3 do plano.
- Pontos de entrega de interrupção, eventos por tipo e esperas, conferidos no disassembly:
  - `8C064A26` espera a bandeja `0x8C24F88A`;
  - o TMU é lido em `8C15D1A2`.

**Revisão do Codex** (`reviews/codex-20261015-134106.md`): 4 achados ALTA e 6 MÉDIA, todos
conferidos e aceitos. A tabela está na seção 10 do plano.

A implementação aguarda a aprovação do plano e as decisões D1-D7.

### N2a: relógio lógico e fila de eventos nativos (2026-10-15)

Plano aprovado como diretriz, com as decisões D1–D6; a D7 ficou aberta. Autorização: só o N2a,
sem commit. Relatório: `docs/N2a-RELATORIO.md`.

**O que mudou:**
- `native/platform/time.h` revisada; `LogicalClock` e `EventQueue` em `native/platform`.
- Os módulos do runtime pedem eventos e instantes a um provedor de tempo
  (`runtime/cpu/time_hooks.h`).
- `--tempo nativo` (padrão): `transitional/time_adapter`. `--tempo legado`: o cálculo de antes,
  reunido em `runtime/cpu/time_legacy.cpp`.
- Os ciclos estimados entram só como instante de compatibilidade.
- `tempo.consumidores.ciclos` 7 → 0, com catraca.

**Portões** (`tools/dist/test_tempo.ps1`, roteiro NEW GAME, relógio fixo):
- A1, A3 e A5 idênticos entre `--tempo legado` e `nativo` em 3 000 (arquivos nativo), 6 000
  (arquivos legado) e 8 000 (os dois provedores de arquivos);
- quadros iguais ao N0 (`B49D68E2165B8505`, 6 000) e ao N1 (8 000);
- A2: 15 999 VBlanks com o devido na linha exata do SPG, 0 entregas antecipadas, atraso
  máximo 2 120 ticks;
- negativos detectados: prioridade, perda, atraso, cancelamento, deriva, relógio em degraus,
  leitura de ciclos fora do provedor e dependências de cabeçalho perdidas.

**Diferença de 2 VBlanks: causa provada sem intervenção.** Um trecho com interrupções mascaradas
funde as interrupções dos VBlanks 21 e 22 no modo de arquivos nativo. O contador de troca de
quadro do SDK (`0x8C2B4488`, `8C08F0E0`/`8C08F332`) fica 1 atrás a partir de V22, e a tela-título
entra um passo de lógica depois. Detalhes na seção 4 do relatório.

**D7:** inventário do TMU feito.
- Todos os chamadores são do SDK; o jogo não lê o TMU.
- `8C1586B8` (CRI): medição de carga, 20 M chamadas em 3 000 VBlanks.
- `8C173044`: espera com timeout.
- A recomendação está na seção 5 do relatório.

**D6:**
- `VSLANG=1033` nos presets não resolve nesta máquina (sem o pacote de idioma inglês).
- A causa é a página de código do terminal.
- Proteções: aviso no configure e o teste `n2_build_incremental`.

**Preview:** o cache de compilação do Builder foi renomeado
(`%LOCALAPPDATA%\BlueStingerRecomp\build.antes-n2a`, `build.n2a-1`) para a Preview final ser
compilada do zero. O executável do N1 ficou em `dist/BlueStinger-Preview.n1-770e3d1` (rollback).

**Revisões do Codex:**

| Revisão | Resultado |
|---|---|
| `reviews/codex-20261015-142429.md` | 4 MÉDIA e 3 BAIXA, todos aceitos e corrigidos, cada um com teste que falha sem a correção. Lista na seção 11 do relatório |
| `reviews/codex-20261015-164027.md` (acompanhamento) | Nenhum achado novo. Uma observação de cobertura, aceita |

**Testes:** 2026-10-15, no código final:
- unitários 204/204 (12 `n2_*`);
- `ctest` com o CDI real 12/12 (`boot_trace` pulado: sem o oráculo), incluindo `n0_fechamento`
  com os negativos da catraca e `n2_build_incremental`;
- `make_builder` e `make_preview`: a Preview foi compilada do zero, com cache novo do Builder.
  Conferido que ela aceita a mutação `relogio_degraus` e recusa a antiga `congelar_relogio`, que
  só existiam antes e depois da última correção;
- `test_preview` 9/9; `test_gameplay` 4/4; `test_builder` 18/18; `test_projeto` 11/11 (mods);
  `test_arquivos` 6/6; `test_ordem` 8/8; `test_tempo` 35/35.

O `test_tempo` falhou duas vezes antes da versão final, ambas por defeitos do próprio teste:
- a mutação de relógio parado travava o jogo;
- o rc vinha vazio com `Start-Process`.

Os portões de conteúdo passaram nas duas tentativas.

### N2b: preparação (2026-10-15)

Autorização: só investigação, planejamento e testes diagnósticos. Plano: `docs/N2b-PLANO.md`.
Commit do N2a: `fdf326a`.

**Achados** (disassembly e traces, roteiro NEW GAME, relógio fixo):

| Tema | Achado |
|---|---|
| Agendamento | Dois níveis. (1) Threads do SDK trocadas na volta da interrupção pelo despachante `8C17DE4C`: thread 0 (principal) e thread 3 (`8C158840`, servidor do CRI), alternando a cada VBlank. (2) Escalonador cooperativo do jogo (`8C0903C0`, `setjmp`/`longjmp`): uma passada por VBlank, 2 tarefas que dormem 1 passo |
| Espera `8C173044` | Espera o fim do render anterior (bandeira `0x8C2D9E3C`, apagada por `8C1725E6` no fim de render), com timeout de 166 ms (`0x28870` µs). Uma vez por render (3 986 em 8 000 VBlanks); **o timeout nunca estourou**; `8C172FE6` é o gancho de conclusão normal |
| TMU | Todos os consumidores classificados: medição (servidor do CRI `8C1586B8` = 20 M chamadas), carimbos, cronômetro, timeouts (`8C173044`, `8C0978BE`, gdFsStop já substituído) e registros. Nenhum é do jogo. Com `BS_TMU_DIAG=quadro` (TMU parado por VBlank), em 8 000 VBlanks: ordem lógica, leituras e quadro idênticos. D7 continua hipótese condicional (revisão do Codex) |
| Thread 3 | Não tem ponto de espera: roda o servidor do CRI até ser preemptada. É o principal risco do N2b |

**Ambiente de build:**
- opção `BS_MSVC_DEPS_JSON`, **desligada**: dependências por `/sourceDependencies` (JSON) e
  `tools/build/msvc_srcdeps.cmake`, independentes de idioma e página de código;
- com a página 850, o modo atual perde a dependência e o JSON mantém;
- projeto inteiro no modo JSON: 0 objetos do projeto sem dependências;
- `n2_build_incremental` ganhou os casos JSON, página 850, caminho com espaço/`#`/`$$` (falha sem
  o escape) e compilador com erro.

**Diagnósticos acrescentados:**
- `BS_CALL_TRACE` com colunas PR e VBlank;
- `BS_TMU_DIAG=quadro`.

Comportamento padrão inalterado: trace de 3 000 VBlanks = referência do N2a. `ctest` 12/12.

**Revisão do Codex do plano** (`reviews/codex-20261015-192135.md`): 2 ALTA, 4 MÉDIA, 1 BAIXA,
todos aceitos. Os principais:
- a thread 3 saiu da lista de esperas;
- `BS_WAIT` nunca escolhe thread;
- a guarda de liveness vira erro;
- critérios com oráculo e negativo;
- escape do depfile.

**Acompanhamento** (`reviews/codex-20261015-192954.md`): 1 ALTA e 2 MÉDIA, de texto, aceitos:
- o componente nativo não escolhe thread (`ITaskScheduler` não é usada para as threads do SDK;
  D4 revista);
- o TMU fica como "classificação preliminar";
- novo critério B3b para as leituras de relógio e estado.

### N2b-0 / N2b-1: diagnósticos e modo sombra (2026-10-16, commit `18a5cc2`)

Autorização: só N2b-0 e N2b-1. **N2b-2 não autorizado.** Relatório: `docs/N2b-RELATORIO.md`.

**Compilação (D10):**
- `BS_MSVC_DEPS_JSON` ligado por padrão (presets, CMake e kit do Builder).
- Defeito do lançador no Debug: `/Fd<pasta>\` fazia o argumento seguinte se fundir a ele
  (C1041). Correção: a barra invertida final vira `/`.
- `n2_build_incremental` ganhou um executável em Debug. Método: o teste **falha** com o lançador
  antigo (C1041) e passa com o novo.
- Release, Debug e mods compilados do zero.

**Modo sombra:**
- `IProgress`/`ProgressMonitor`;
- `BS_WAIT`/`BS_WAIT_END` nos 3 pontos do config;
- `Sh4Context::polls`;
- evento esperado por espera, notificado só depois que o assinante o aplica;
- VBlank-In colapsados no adaptador.

Método: trace de ordem completo igual ao legado byte a byte (8 000 VBlanks, duas rotas de
arquivos); desempenho igual (17,27 × 17,38 ms por campo).

**Medidas** (8 000 VBlanks, NEW GAME):
- **Espera do VBlank:** 3 792 episódios, todos com VBlank-In entregue no episódio.
- **Espera do render:** 17 episódios, todos com o fim de render; maior episódio 1,49 M ticks
  (estouro = 33,2 M).
- **Thread 3:** ~113 mil pontos de controle por VBlank.
- **Janelas sem espera:** até 17,2 M (cargas); `kDefaultStallUnits` = 64 M.

**CRI (thread 3):**

| Condição | Resultado |
|---|---|
| 0 entradas MW | **descartada**: muda as leituras de arquivo (`pular`) |
| Sem MW, sem ADX e sem stream (gerenciador `0x8C2DA958`) | sem efeito lógico (TN, CL, SL, quadro iguais). O controle só de tempo (`repetir3`) reproduz as outras diferenças (AICA no vídeo, SPG). Mas só vale em boot e menus (27% das passadas). **Correção (2026-10-16, depois do commit):** a diferença "AICA no vídeo" era defeito do artefato AUD (escritas do P4 contadas como áudio); com o AUD corrigido, as escritas na AICA são iguais. Ver a seção seguinte |

**Testes:** `tools/dist/test_n2b.ps1` (S1–S4, B1–B9 com negativos; B7, B10 e B11 parciais).

**Regressão completa** (2026-10-16, em sequência):

| Suíte | Resultado |
|---|---|
| Unitários | 210/210 |
| `ctest` | 12/12, sem e com a imagem |
| `test_preview` | 9/9 |
| `test_gameplay` | 4/4 |
| `test_builder` | 18/18 |
| `test_projeto` | 11/11 |
| `test_arquivos` | 6/6 |
| `test_ordem` | 8/8 |
| `test_tempo` | 35/35 |
| `test_n2b` | 44/44 |

O `test_tempo` falhou na primeira rodada. O A2 lia como entregas os registros diagnósticos
`RenderStart` do N2b-0. Corrigido no teste; conferido: 0 antecipadas, atraso máximo 2 120 ticks,
o mesmo do N2a.

**O que bloqueia o N2b-2:** relatório, seção 8. O principal é a ociosidade da thread 3 no
gameplay.

### N2b-2: investigação dos bloqueios (2026-10-16, sem commit)

Pedido: eliminar os bloqueios do N2b-2 sem implementá-lo. Relatório: `docs/N2b-BLOQUEIOS.md`.
Diagnósticos novos, desligados por padrão (precisam do build com mods):
- `BS_CRI_DIAG=fluxo|pular5|repetir5|consumidor`;
- `BS_CRI_FLUXO_LOG`, `BS_CRI_MEDIDORES`, `BS_CRI_SNAP_VB`;
- `BS_AICA_MUTACAO`.

**Thread 3 (CRI):**
- `+48` do gerenciador de streams = "buffer cheio" (disassembly de `8C174888`..`8C174E16`).
- Medição não invasiva (todos os artefatos iguais com e sem ela).
- No gameplay, **nenhuma passada com trabalho**:
  - área 1, 8 000 VBlanks: 90 650 com trabalho em 2 666 236, quase todas do vídeo;
  - exploração, 16 000 VBlanks: o mesmo trabalho, nenhum nos 8 000 VBlanks a mais.
- O stream ambiente (`E012BENV.WAV`) enche e para: sem o ARM7, nada o consome.
- **Observador independente:** toda a RAM principal e a de som, em 6 janelas de 3 VBlanks (menu,
  fim do vídeo, enchimento, gameplay da área e da exploração, gameplay com TMU parado).
  - Uma passada ociosa sem evento só grava os medidores do TMU.
  - Com o TMU parado por quadro (D7), não muda nada.
- **Transições sem evento:** 7 contando só o início da passada; 1 contando também o meio dela
  (consumo pelo relógio de software, TMU); **0 com o TMU parado por quadro** (D7).
- **Bloqueio até o próximo evento** (`pular5`, área 1 com 8 000 VBlanks e exploração com 16 000,
  build final): TN (sem normalização), IRQF, CL, SL, AUD e quadro iguais à referência.
  - Com uma época de eventos incompleta (sem os eventos do Holly mascarados), a TN diferia numa
    cessão de exclusão mútua `8C1588AA`.
  - Essa cessão só existe com preempção no meio da passada. Ela é marcada causalmente
    (`BS_CRI_CESSAO_LOG` na cessão `8C17DDCC`, PRs `8C1588C2` e `8C158854`) e fica como exceção
    proposta.
- Controle `repetir5`: tudo igual.
- O S4 por VBlank é artefato de tempo: o controle já o muda.

**Áudio:** o AUD do N2b-0 contava as escritas do P4 (`hwWrite` tenta a AICA antes do P4):
- eram 424 M dos ~425 M; as escritas reais são 1 023 386;
- corrigido, sem mudar a execução;
- leituras da RAM de som numa coluna nova (`audrd`);
- o oráculo fica igual em todos os experimentos e detecta os dois negativos (`BS_AICA_MUTACAO`):
  escrita perdida e escrita registrada mas não aplicada;
- o segundo negativo mostrou que **o estado do driver de som é entrada da lógica**: com o bloco de
  estado em 0, a biblioteca manda comandos e o vídeo muda (8 447 leituras em vez de 7 750);
- limitação: sem o ARM7, a biblioteca de som não manda comandos no gameplay (0 escritas).

**B10:**
- primitiva `bs::mods::wait`/`wait_end`/`poll` (unitário `n2b_mod_wait_primitive`);
- `test_projeto` caso 4b: mod da espera do VBlank, com e sem a primitiva;
- a falta de progresso agora sai na hora, com o trabalho de cada thread na janela e o PC onde ela
  mais gastou. A regressão pegou duas versões erradas: a thread que cruzou o limite (a 3) e o
  último PC (a cessão ao CRI no render).

**B11** (classificação corrigida pela revisão: `0x8C1DD1C4` é contador lógico; os carimbos são
lidos só para cópia):

| Caso | Resultado |
|---|---|
| Medidores do CRI, legado × sombra | iguais |
| TMU parado por quadro | lógica igual, medidores diferentes |
| Consumidor artificial | detectado nas leituras |

**D8:** a rota de 16 000 VBlanks não aumenta a janela máxima (17,2 M, entrada na área). Não há
roteiro reproduzível com troca de sala ou save/load (uma tentativa às cegas falhou).

**B4:** a sequência de eventos de lógica (71 790) é idêntica entre os provedores de arquivos.
Muda só o VBlank em 4 cargas, mais 1 ciclo de atraso no boot. Proposta: D9(a) nos dois
provedores no N2b-2.

**Teste novo:** `tools/dist/test_cri.ps1` (C1–C5, A1, B11, com negativos).

**Regressão definitiva** (2026-10-16, 14:45–16:25, em sequência, código congelado antes do
início):

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

As rodadas anteriores pegaram e corrigiram:
- a catraca do N0 (`c->cycles` em `mods::poll`);
- duas versões erradas da mensagem de falta de progresso.

Elas foram interrompidas e repetidas do zero.

**Catraca do N0:** a primeira versão de `bs::mods::poll` lia `c->cycles` à mão. O `n0_fechamento`
acusou `tempo.consumidores.ciclos 0 → 1`. A função passou a usar `BS_CYCLES`/`BS_POLL`.

### Proposta de sequência para o N2b-2 (2026-10-16, depois de `246e6b3`, sem commit)

Documento: `docs/N2b-PROPOSTA.md`.

**Medidas novas** (observador de RAM com contadores para passadas com trabalho):
- no meio dos dois vídeos (V2150, V4550; também com o TMU parado), ~1 005 passadas com MW ativo
  e sem evento deixam a RAM principal e a de som inalteradas;
- isso é indício, não prova: o observador não cobre VRAM, MMIO, filas e o estado interno.

**Inventário pelo aviso de falta de progresso:**
- as janelas longas da thread 0 são computação (`8C0100EE`, `8C1C43C8`);
- durante o vídeo, a thread 0 fica suspensa.

**Esperas temporais não declaradas** (achadas pela revisão, confirmadas no disassembly):
- SPG vsync em `8C172624`/`8C17262A`;
- `ISTNRM` VBlank-In em `8C172804`;
- polling de DMA `C2DST` em `8C1C1DC2`.

**Recomendação:**
1. P0 (inventário das esperas temporais) e P1 (observador ampliado e predicado do MW/ADX);
2. decisões D7, D9(a), S4, exceção e D11;
3. N2b-2;
4. áudio e vídeo nativos sobre o relógio lógico;
5. o protocolo do driver de som analisado em paralelo.

### N2b-2: investigação P0/P1 dos bloqueios (2026-10-16, depois de `246e6b3`, sem commit)

Pedido: P0 (inventário das esperas temporais) e P1 (ociosidade do Sofdec/ADX), sem implementar o
N2b-2. Documentos:
- `docs/N2b-PROPOSTA.md`, seções 8–12;
- `docs/N2b-ESPERAS.md`.

**Cobertura de todas as medidas:** **um** roteiro (NEW GAME até a primeira área), em 8 000 e
16 000 VBlanks.

**Ferramentas** (diagnóstico; o produto não muda):
- **build de investigação** `-DBS_WRITE_WATCH=ON`, com a sonda de escrita e de poll:
  - efeito líquido por passada: RAM, MMIO/VRAM/AICA, store queue, host, GD-ROM, TMU;
  - voltas exatas por back-edge, com os retornos de chamada separados;
  - voltas sem progresso;
- `BS_CRI_DIAG=efeito|pular6|repetir6|pular6r|pular6s`;
- `tools/dist/test_efeito.ps1` (E1–E7);
- `tools/dev/n2b_esperas_estaticas.py`, `n2b_lacos_quentes.py`, `n2b_classifica_lacos.py`;
- dados brutos com proveniência (hashes, comandos) em `analysis/n2b/`.

**P0, inventário (método: voltas sem progresso em todos os laços executados, mais o varredor
estático e a classificação estrutural):**
- dos 833 laços executados, seis têm voltas sem progresso, e os seis são esperas temporais:
  - duas declaradas: VBlank `8C064A26` e render `8C17306A`;
  - quatro não declaradas: `8C172804` (VBlank-In), `8C17262A` (vsync), `8C172EF2` (callback de
    VBlank) e **`8C1C6D08`**;
- `8C1C6D08` é nova: espera a linha de varredura do `SPG_STATUS` voltar a 0, por um getter
  chamado por registrador, em `f_8C1C6B3C`. O varredor estático não a via;
- com as de 0 voltas (`8C172624`, `8C1C6CB0`), são seis laços de espera do boot (V2 e V13), cada
  um executado uma vez;
- polling satisfeito sincronamente: `8C1C1DC2`, `8C096EE0`, `8C09724C`;
- o resto é computação.

**P1, ociosidade** (`test_efeito.ps1`: 17 casos, 0 falhas, E7 em SKIP):

| Medida | Resultado |
|---|---|
| Passadas sem evento | 2 653 809, das quais **81** com efeito líquido |
| Com MW/ADX ativo | 89 610 de 89 661 sem efeito |
| Violações | **0** (também com o TMU parado) |
| Supressão sob o modelo atual (`pular6`) | TN, IRQF, CL, SL, AUD e quadro iguais ao legado; controle igual |
| Negativo `pular6r` (ignora a RAM) | detectado |
| Negativo `pular6s` (ignora a store queue) | **não exercitado**: nenhuma passada tem efeito só por ela |
| D7 + supressão | só as 4 cessões marcadas somem |

O bloqueio real não foi testado: é o portão do N2b-2 experimental.

**D11:** com as esperas do boot declaradas em modo sombra, o boot se divide em:
- V0–V2: 1,29 M;
- V2–V13: 4,20 M;
- V13–V33: 9,72 M.

Mais 9 trechos de trabalho de 2 a 25 VBlanks (seção 9.3 da proposta).

**Custo** (sequencial, 3 000 VBlanks):

| Configuração | Custo |
|---|---|
| Build com mods | +0,4% |
| Sonda compilada e desligada | +3,1% |
| Detector ligado | +9,3% |
| Produto (Release) | inalterado |

"< 0,1% com o bloqueio" é só uma extrapolação linear.

**Critérios de desbloqueio:**
- U1 **parcial**;
- U2–U4 cumpridos em uma rota;
- U5–U8 pendentes;
- **N2b-2 continua bloqueado.**

**Regressão completa** (2026-10-16, 21:22–23:26, sequencial, depois do código estabilizado):

| Etapa | Resultado |
|---|---|
| Builds release, debug e mods | ok |
| Unitários | 211 |
| ctest, sem e com o disco | 100% |
| `make_builder`, `make_preview` | ok |
| `test_preview` | 9/0 |
| `test_gameplay` | 4/0 |
| `test_builder` | 18/0 |
| `test_projeto` | 13/0 |
| `test_arquivos` | 6/0 |
| `test_ordem` | 8/0 |
| `test_tempo` | 35/0 |
| `test_n2b` | 44/0 |
| `test_cri` | 33/0 |
| `test_efeito` | 17/0, E7 em SKIP |

A primeira tentativa parou no `make_preview`, e com razão: havia um `bluestinger.log` com o caminho
de desenvolvimento dentro de `dist/BlueStinger-Preview`. Ele vinha de uma execução diagnóstica de
cobertura às 17:07. O log foi movido para `analysis/n2b/` e a regressão foi refeita do início.
Os backups da Preview ficaram intactos.

### N2b-2 experimental: tempo lógico sem ciclos estimados (2026-10-17, sem commit)

Autorização: N2b-2 **só experimental, atrás de opção** (`--tempo experimental`). O padrão continua
`--tempo nativo`. Relatório: `docs/N2b2-RELATORIO.md`. Teste: `tools/dist/test_n2b2.ps1`.

**Mecanismo:**
- o instante lógico só anda quando o contexto corrente chega a um ponto de espera declarado, e
  só até o próximo evento real (fila do Holly e do GD-ROM; nas esperas do SPG, a próxima troca de
  linha);
- TMU, SPG, MDST e RTC leem esse instante (D7);
- 12 pontos de espera novos no config:
  - os 6 laços de espera do boot;
  - 5 esperas síncronas (DMA ch2 e `SB_FFST`);
  - o back-edge condicional da thread 3;
- a thread 3 bloqueia depois de passada ociosa, pelo critério `pular6`, com RAM rastreada pelo
  sistema;
- falta de progresso, deadlock e espera síncrona que dá volta viram erro com threads, PCs e
  esperas.

**`test_n2b2.ps1`: 17 casos, 0 falhas** (exploração 16 000 e área 1 8 000 VBlanks):

| Critério | Resultado |
|---|---|
| Leituras | mesma sequência (7 915) que o estável |
| Quadros | mesma sequência de 2 517 trocas do boot ao gameplay (título, menus, dois vídeos, cargas); com as entradas alinhadas por passo de jogo, iguais na rota inteira, salvo a transição do menu de itens (D11) |
| Áudio | mesmas 1 023 002 escritas na AICA na rota inteira, sem as cópias de guarda do anel |
| Eventos | 99,94% dos VBlanks com padrão existente no estável |
| Provedores (D9a) | legado = nativo VBlank a VBlank |
| Timeout de render (B5) | preservado |
| Negativos de liveness | 3, todos detectados com thread, PC e espera |
| Detector da thread 3 | necessário (bloquear sempre muda leituras e quadros) |
| Determinismo | duas execuções idênticas |
| Estável inalterado | byte a byte igual ao executável anterior |
| Mods | o build com mods dá o mesmo resultado |
| Desempenho | experimental −12% a −14% de trabalho por campo |

**SKIP:** o DMA ch2 não é alcançado na rota; o negativo `cri_ignora_ram` não é sensível.

**Diferenças justificadas (D11):**
- o trabalho não consome VBlanks: as cargas e o boot são instantâneos, e a partida dos vídeos é
  mais curta. A área 1 abre 42 VBlanks antes;
- o estável perde renders, o experimental não;
- o áudio só difere nas cópias de guarda do anel;
- no mesmo passo de jogo, 194 palavras de RAM diferem (TMU, contadores de VBlank e passadas,
  pilha da thread 3), nenhuma na região de variáveis do jogo.

**Gameplay (revisão do Codex):** a divergência depois do primeiro botão vinha da entrada. O roteiro
é indexado pelas leituras do controle, e o estável fez 26 a mais nas cargas. Com as entradas
alinhadas, quadros, leituras e áudio são iguais na rota inteira, salvo a transição do menu.

**Desempenho do estável:** +2,0% sobre o executável anterior (cinco rodadas pareadas), dentro do
B9. **Custo conhecido e aceito** pelo mantenedor em 2026-10-17 (tolerância B9 de 5%). Não será
investigado nem otimizado agora.

**Defeito encontrado e corrigido:** `pvr_spg_next_line_change` (arredondamento no limite de
linha), com unitário sensível.

**Rollback:**
- `analysis/rollback-antes-n2b2/` (código gerado e executáveis, com hashes);
- `dist/BlueStinger-Preview.p0p1-246e6b3`.

**Regressão completa** (2026-10-17, 03:36–05:35, sequencial):

| Etapa | Resultado |
|---|---|
| Builds release, debug, mods e investigação | ok |
| Unitários | 212 |
| ctest, sem e com o disco | 100% |
| `make_builder`, `make_preview` | ok |
| `test_preview` | 9/0 |
| `test_gameplay` | 4/0 |
| `test_builder` | 18/0 |
| `test_projeto` | 13/0 |
| `test_arquivos` | 6/0 |
| `test_ordem` | 8/0 |
| `test_tempo` | 35/0 |
| `test_n2b` | 44/0 |
| `test_cri` | 33/0 |
| `test_efeito` | 17/0 |
| `test_n2b2` | 17/0, com 2 SKIP declarados |

Na regressão, o desempenho deu 17,23 ms por campo no estável e 14,75 ms no experimental.

A primeira tentativa parou no `n0_fechamento`, a catraca do `bs-hwscan`: `sh4_wait_point` fazia
`next_event = cycles`, uma leitura direta de `c->cycles` em código novo do runtime. A correção usa
`next_event = 0`, o mesmo idioma do fim do DMA do canal 2, com o mesmo efeito.


## Revisões independentes (Codex)

### 2026-10-17: N2b-2 experimental, duas rodadas. Relatórios em `reviews/codex-20261017-015745.md` e `-024221.md`

Todos os achados foram aceitos. O detalhe está em `docs/N2b2-RELATORIO.md`, seção 7.

| Rodada | Achados principais | Ação |
|---|---|---|
| 015745 | [MÉDIA] equivalência de gameplay não demonstrada; N3 aceitava vários quadros de boot; N4 não afirmava a rota inteira; N11 passava com artefatos ausentes. [BAIXA] N6 por ponto de controle; limite de evento vencido acumulava; unitário sem fase nem entrelaçado | Bissecção da RAM: a divergência vinha do índice do roteiro (leituras do controle, 26 a mais no estável). Com as entradas alinhadas, o gameplay é igual, salvo o menu de itens. Testes endurecidos; limite zerado a cada entrega; unitário com fase do BIOS e entrelaçado |
| 024221 | [MÉDIA] N3b limitado só de um lado e sem o fim do experimental; N4 sem a área 1. [BAIXA] faixa do quadro de boot; término no N6 | Comparação por ponto lógico (leitura alinhada) até o último ponto comum, com a exceção limitada pelos dois STARTs do menu; áudio da área 1; faixa V4–V13 conferida no log bruto; término exigido |

Verificado sem problema pelo Codex:
- os ciclos não movem o relógio no experimental;
- o alvo considera o display e o disco;
- o poll forçado preserva o SPC;
- os tipos das esperas, no binário;
- o detector reproduz o `pular6`;
- o modo estável não recebe estado do experimental, só custo;
- o índice do roteiro é o `GetCond`.

### 2026-10-16: investigação P0/P1 do N2b-2, três rodadas. Relatórios em `reviews/codex-20261016-182653.md`, `-194556.md` e `-204336.md`

Todos os achados foram aceitos. As ações de cada um estão em `docs/N2b-PROPOSTA.md`, seção 11.

| Rodada | Achados principais | Ação |
|---|---|---|
| 182653 | [ALTA] falsos negativos do detector (store queue, UNF do TMU, `xferDue`); inventário não auditável; `pular6` ≠ bloqueio real; boot com as próprias esperas; "duas rotas"; contrato de duração da mídia | Detector ampliado; varredores versionados; U3 reescrito; boot dividido; texto "uma rota"; modo fiel × modificado |
| 194556 | resumo ainda dizia "duas rotas"; U1 heurístico; `BS_WW_VOLTAS` misturava retornos de chamada; E7 vazio aprovado; "< 0,1%" | Texto corrigido; todos os back-edges e o critério da volta sem progresso (**achou `8C1C6D08`**); `BS_POLL_RET`; E7 em SKIP; extrapolação rebaixada |
| 204336 | estado sem `VBR`/`SSR`/`SPC`/`SGR`/`DBR`; `Last` global (falso negativo em laço externo); proveniência; E7 com artefato vazio; "sem custo" | Estado completo; 16 laços ativos (resultado idêntico); proveniência com hash das fontes; `Same` no E7; texto do custo |

Verificado sem problema pelo Codex:
- o produto sem sonda no caminho quente;
- a nova espera no binário;
- os 13 laços de endereço fixo como computação;
- o tratamento do delay slot;
- a conclusão de que o N2b-2 não está desbloqueado.

### 2026-10-16: proposta de sequência para o N2b-2. Relatório em `reviews/codex-20261016-163758.md`

| Achado | Decisão | Ação |
|---|---|---|
| [ALTA] A ociosidade no vídeo não decorre de um observador só de RAM | **Aceito** | Reformulada como indício; o P1 exige observador ampliado |
| [ALTA] Esperas temporais não declaradas (SPG, ISTNRM; polling de DMA) | **Aceito**, confirmado no disassembly | E9 e etapa P0 |
| [ALTA] A D11 podia esconder perda de eventos | **Aceito** | D11 como mudança de produto, só em trecho provado atômico |
| [MÉDIA] Registro de escrita (P1b) insuficiente, custo subestimado | **Aceito** | Detector conservador de efeitos, com limites |
| [MÉDIA] VBlanks mínimos por carga = espera fictícia | **Aceito** | Removido |
| [BAIXA] Contadores do observador não zerados por sessão | **Aceito** | Corrigido em `cri_diag.cpp` (não commitado) |

### 2026-10-16: segunda e terceira revisões das correções. Relatórios em `reviews/codex-20261016-141648.md` e `reviews/codex-20261016-144322.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] A TNC por intervalo da chamada a `8C1588AA` continuava temporal: o callback de VBlank pode trocar de thread dentro do intervalo | **Aceito** | Marcação causal: a cessão `8C17DDCC` mascara as interrupções e salta ao despachante, então o próximo evento é a troca pedida. Só os PRs `8C1588C2` e `8C158854` são marcados; o teste falha se uma marca não cair numa troca. Resultado: 8 marcas (referência) e 8 (bloqueio), todas trocas |
| [MÉDIA] `0x8C1DD23C` (bandeira lógica) estava na faixa dos medidores | **Aceito** | Faixa `0x8C1DD1C8`–`0x8C1DD238` |
| [MÉDIA] O negativo `registrada:` não exigia consequência de comportamento | **Aceito** | Exige leituras e ao menos um de TN, CL, SL, quadro diferentes (mudam os quatro) |
| [BAIXA] "PC onde mais gastou" é atribuição por lote | **Aceito** | Mensagem: "maior lote em" |
| [BAIXA] Captura de RAM com o TMU parado fora da regressão | **Aceito** | `fluxo_tq` com captura: zero mudanças sem evento e zero só nos medidores |
| [BAIXA] (3ª) O teste não exigia marcas no bloqueio | **Aceito** | Exige quantidade igual à da referência |

A terceira verificação confirmou no disassembly a premissa causal e os PRs.

### 2026-10-16: investigação dos bloqueios do N2b-2 (thread 3, áudio, B10/B11, D8, B4). Relatório em `reviews/codex-20261016-105307.md`

O Codex confirmou como corretos:
- o `+48` e o fluxo do gerenciador de streams;
- a mecânica de `8C1588AA`;
- a correção do AUD;
- `bs::mods::wait` contra `BS_WAIT`, e o mod do caso 4b contra `8C064A20`;
- os contadores novos e a assinatura de `progress`.

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] A TNC removia qualquer par adjacente 0→3→0 sem provar a causa | **Aceito** | `BS_CRI_CESSAO_LOG`: intervalos do trace de cada chamada a `8C1588AA` com troca. A TNC remove só as trocas de dentro |
| [MÉDIA] "Medidores e carimbos só escritos" era falso (`8C158A50` lê `0x8C1DD1C4`; `8C15886E`/`8C158884` leem 228/238) | **Aceito** | Confirmado no disassembly. `0x8C1DD1C4` (contador lógico) saiu da faixa dos medidores; 228/238 são lidos só para cópia em 224/230; a decisão em `8C158864` é pela bandeira lógica `0x8C1DD23C` |
| [MÉDIA] C1 comparava projeções | **Aceito** | Arquivos brutos comparados: idênticos |
| [MÉDIA] C2 circular, com tolerância e passando com 0 = 0 | **Aceito** | Sem tolerância (último trabalho = VBlank da última leitura); exige violações > 0 e eventos durante as passadas > 0; observador de RAM independente |
| [MÉDIA] Prova pela RAM em duas janelas, com exclusão larga da pilha | **Aceito** | Toda a RAM principal e de som; exclusão só abaixo do SP da entrada; texto como evidência amostral |
| [BAIXA] O negativo do AUD tirava a própria escrita do log | **Aceito** | Novo negativo de sensibilidade (escrita registrada, não aplicada), detectado pelas leituras |
| [BAIXA] O GD-ROM faltava nos eventos de despertar | **Aceito** | Incluído |

### 2026-10-16: N2b-0/N2b-1 (lançador, modo sombra, diagnósticos, `test_n2b.ps1`). Relatório em `reviews/codex-20261016-002552.md`

O Codex confirmou como corretos:
- o lançador (falha do compilador, barra final, escapes);
- os 3 back-edges reais (`89FB`, `8BF0`, `8BF2`);
- `BS_WAIT` só no ramo tomado e `BS_WAIT_END` no fall-through;
- o modo sombra sem escrita no estado do guest.

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [MÉDIA] S4 media polls agregados, comparava sombra com sombra; o negativo passava com artefato vazio | **Aceito** | Artefato de chamadas (`BS_CALL_TRACE` em `8C1586B8` e `8C174888`, quantidade e hash da sequência por VBlank), legado × sombra; `Differ` exige os dois artefatos não vazios; negativo `BS_CRI_DIAG=menos1` (uma passada a menos por VBlank). Resultado: 3 376 VBlanks e 1 000 742 passadas, iguais; negativo detectado |
| [MÉDIA] O Holly não via dois VBlank-In no mesmo atendimento | **Aceito** | Confirmado: com `fundir_vblank`, o Holly continuava com 14 e o adaptador agora conta 30 colapsos. O positivo exige 0 colapsos; o negativo exige mais que a referência. O plano pede 0 fundidos, mas o legado tem 14 no boot: o critério ficou "igual ao legado", e o desvio está no relatório |
| [MÉDIA] "Liberada por" era o último evento global, notificado antes do assinante | **Aceito em parte** | O monitor passa a ser notificado depois que o assinante aplica o evento; cada espera declara o evento esperado; S2 exige o evento esperado entregue no episódio. Teste `n2b_progress_expected_event`. Isso prova a entrega, não a transição da condição do jogo (limitação registrada) |
| [MÉDIA] B4 sem troca de provedor de arquivos; B5 não provava o estouro | **Aceito** | B5: saída sem o fim de render e episódio ≥ 0,9 × 166 ms (54,4 M ticks; referência 1,49 M). B4: comparação cruzada acrescentada. Ela mostra a diferença conhecida do N1 (tempo de carga, `IWP20`×`IWP15`), por isso fica informativa; o contrato P1 continua no `test_arquivos`. A igualdade de TN entre provedores é meta do N2b-2 |
| [MÉDIA] B7, B10 e B11 incompletos | **Aceito** | Declarados parciais no script e no relatório. B7: contagem de deadlocks nos positivos, e fila vazia → deadlock no unitário. B10 = as demais suítes. B11 só a ordem lógica com o TMU parado |

Depois das correções: unitários 210/210; `test_n2b` rodado de novo na regressão completa.

### 2026-10-15: mudanças pendentes desde a5a1fe4 (dados sem CDI, Builder, NEW GAME, YUV/VQ, mods). Relatório em `reviews/codex-20261015-002752.md`

Escopo e riscos em `reviews/relatorio-pendente-20261014.md`. O Codex confirmou como corretos:
- a vtable de NEW GAME (bytes em 8C1DBF58 e 8C194580);
- `BSG_FN` em chamadas diretas, tail calls e resolvidas;
- a ordem de reconstrução dos setores;
- o LRU de 16 arquivos;
- os deslocamentos de mipmap (tabela do manual, p. 148);
- UYVY e os coeficientes YUV (seção 3.6.1.2).

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] `disc_meta.*` sem SHA-256: com hard links, alteração do mesmo tamanho passava pelo reaproveitamento e pelo `--verify-data` | **Aceito** | Formato de dados **2**: o manifesto tem o SHA-256 de `disc_meta.idx/.bin`, conferido primeiro em `check_runtime_files`. O teste `runtime_data` (CDI real) altera 1 byte de `disc_meta.bin` e exige a detecção. Dados no formato 1 são recusados com mensagem, e o Builder prepara de novo (marca muda) |
| [ALTA] Falha ao arquivar a versão anterior (`rename` com `ec` ignorado) e sucesso relatado | **Aceito** | `archivePrevious`: uma `.anterior` nossa só sai depois que a nova entra (vai para um nome temporário e volta se der errado); falha lança dizendo onde a anterior ficou; nomes `.anterior-<data>-<aleatório>` únicos; sobras `.old-*` marcadas são **arquivadas, não apagadas**. Teste com `.anterior` presa (arquivo em uso) |
| [MÉDIA] YUV422: macrobloco são duas metades 16x8 (U, V, Y esq., Y dir.), não U128/V128/Y256 | **Aceito** | Evidência: exemplo yuv422 do KallistiOS. Conversor corrigido. O teste agora tem padrão diferente nas 6 regiões e confere pixels das duas metades (o anterior montava o layout errado e não distinguia) |
| [MÉDIA] Gravação de `data.ok` sem conferência | **Aceito** | Arquivo temporário, `close`, teste do stream e `rename`; falha é erro da etapa |
| [BAIXA] `mods::reset()` não desfazia substituições de endereços sem slot (HLE) | **Aceito** | Conjunto de todos os endereços substituídos, limpo inteiro no `reset`. Teste com endereço HLE |
| [BAIXA] `mem` do oráculo quebrava com tamanho não múltiplo de 4 | **Aceito** | Exige múltiplo de 4 |

Depois das correções:
- 160 unitários e `ctest` 7/7 (com o CDI real);
- distribuições remontadas (dados no formato 2);
- `test_builder` 18/18, `test_preview` 9/9, `test_gameplay` 4/4, `test_projeto` 9/9.

### 2026-10-15: plano do port nativo (planejamento). Relatório em `reviews/codex-20261015-003938.md`

Nova diretriz do mantenedor: port nativo sem simulação de hardware no executável final.
- A v1 de `docs/PLANO-PORT-NATIVO.md` foi revisada pelo Codex: 6 achados altos e 5 médios,
  **todos aceitos** e incorporados na v2 (tabela na seção 9 do plano).
- Principais mudanças:
  - manifesto de acessos a hardware com prova de fechamento (o Codex achou 439 cargas da
    faixa de store queue em 288 funções, além de Holly/AICA/VRAM/DMAC);
  - duas evidências por função identificada do SDK;
  - fronteiras em três camadas com slots sempre ativos;
  - modelo de interoperabilidade antes de N1;
  - N2 dividida em a/b/c;
  - IR de render na N7a;
  - critério final operacional em vez de "100% das funções";
  - portões por contrato normalizado e matriz de cobertura até a campanha;
  - riscos legais e de licenças com revisão jurídica antes de distribuir.
- Métrica corrigida: 71 999 operações de FPU (`bs::op::F*`); a v1 dizia ~115 mil porque
  contava comentários.
- **O plano aguarda aprovação**; nenhuma mudança estrutural foi feita.

### 2026-10-14: Builder (núcleo, compilador, processos, janela, distribuição). Relatório em `reviews/codex-20261014-160223.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Restauração do rollback ignorada; a mensagem dizia "anterior mantida" mesmo sem ela | **Aceito** | A restauração é conferida (`BlueStinger.exe` no destino). Se falhar, a mensagem diz onde está a versão anterior e que ela volta sozinha (backup marcado) |
| [ALTA] Cancelar durante a troca era ignorado e podia terminar como sucesso | **Aceito em parte** | A troca é o ponto sem volta. O cancelamento vale até o primeiro rename (`SwapCancelled`, testado). A janela desabilita "Cancelar" ao entrar em "instalando". Sucesso depois desse ponto é real (a versão nova está instalada) |
| [ALTA] Falhas de `CreateJobObject`/`SetInformationJobObject`/`AssignProcessToJobObject`/`UpdateProcThreadAttribute` ignoradas | **Aceito** | Todas conferidas antes de `ResumeThread`; em falha, o filho suspenso é encerrado e o processo conta como não iniciado. Cancelamento: `TerminateJobObject` com `TerminateProcess` de reserva, e de novo no fim (netos tardios) |
| [MÉDIA] Preservação de arquivos do usuário descartava qualquer DLL na raiz e tudo em `licencas` | **Aceito** | Lista do pacote (`.bs-package`) gravada em cada instalação. Na troca, só sai o que a lista anterior diz. Teste: DLL do usuário e `licencas/minha.txt` ficam; arquivo do pacote antigo sai |
| [MÉDIA] Recuperação de `.old-*` escolhia por data da pasta, sem marca | **Aceito** | Marca `.bs-install` (instante FILETIME). Só backups marcados são recuperados (o mais recente) ou apagados. Teste com três sobras, uma sem marca, que fica intocada |
| [MÉDIA] `longPathAware` ausente; kit longo consultado antes da cópia | **Aceito** | `longPathAware` nos dois executáveis; kit/SDL3 acima de 200 caracteres recusados antes de qualquer acesso, com mensagem |
| [MÉDIA] Kit copiado sem allowlist | **Aceito** | `git ls-files --cached --others --exclude-standard`: tudo o que o `.gitignore` exclui (dados do jogo, `generated/`) fica fora. A checagem de amostras continua como segunda barreira |
| [BAIXA] Pasta de pacote com nome de 32 bits, sem criação exclusiva; o guard podia apagar pasta alheia | **Aceito** | `CreateDirectoryW` exclusivo, sufixo de 64 bits, marca `.bs-stage`; o guard e a limpeza só apagam pastas marcadas. Trava `.bs-lock` (uma execução por saída) |
| [BAIXA] `%`/`&`/`^` no caminho do `vcvars64.bat` interpretados pelo `cmd` | **Aceito** | Caminho via `BS_VCVARS_PATH`, **sem `call`**: o `call` faz uma segunda expansão de `%`, o que foi confirmado em teste. Teste com compilador falso em `bs tc %TEMP% & (x) ^`; com `call`, o teste falha |

### 2026-10-14: distribuição Preview (main_sdl, montagem, testes). Relatório em `reviews/codex-20261014-152633.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Callback do diálogo do SDL escrevia em estado da pilha, sem sincronização; com `SDL_EVENT_QUIT` o estado morria antes do callback | **Aceito** | `SDL_dialog.h` documenta callback em outra thread. Estado em `shared_ptr` (o callback segura uma referência) com mutex |
| [ALTA] `test_preview.ps1 -Work` apagava recursivamente qualquer pasta dada | **Aceito** | O teste usa só a subpasta `bs-preview-teste` com marca própria; sem a marca, recusa |
| [ALTA] Troca da Preview sem rollback: falha entre as renomeações + nova tentativa podia perder a última cópia | **Aceito** | Sobra `.old` sem Preview é restaurada primeiro; backup com nome único; `try/catch` restaura; cópias antigas só saem depois da nova instalada; saída em raiz/repositório recusada. Teste `test_make_preview.ps1` |
| [MÉDIA] Falha do diálogo tratada como cancelamento (código 0) | **Aceito** | `list == NULL` guarda `SDL_GetError()`; mensagem e código 1 |
| [MÉDIA] Falhas de textura/apresentação davam janela preta com código 0 | **Aceito** | Criar a textura falha de vez; atualizar/limpar/apresentar falhando 120 quadros seguidos vira erro, código 1 |
| [MÉDIA] Caminhos absolutos do build no exe (`__FILE__` dos `SDL_assert_release`) | **Aceito** | Confirmado por busca binária (8 ocorrências, todas do SDL). `SDL_ASSERTIONS=disabled`; a montagem agora procura caminhos de desenvolvimento e falha se achar |
| [BAIXA] Caso somente leitura não testava o ini | **Aceito** | Novo caso com imagem válida na pasta somente leitura: ini UTF-8 em LOCALAPPDATA, nada na pasta |

### 2026-10-14: executável nativo (GameHost, SDL3, fibras, boot nativo, poll, render paralelo). Relatório em `reviews/codex-20261014-143829.md`

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Se a janela não abrisse, a fonte de entrada era destruída com a thread do jogo ainda lendo | **Aceito** | `main` agora pede a parada e espera o `GameHost` antes de trocar ou destruir a fonte, em todos os caminhos |
| [MÉDIA] O MXCSR fixo só valia na thread que chama, não nos workers da resolução paralela | **Aceito** | `RenderFpEnv` instalado em cada worker. Teste `render_independent_of_worker_fp_mode`: suja o MXCSR das threads do pool e confere diretamente o ambiente dos workers. O hash da cena não é sensível ao arredondamento, por isso a checagem é direta. **Sensibilidade comprovada**: sem a proteção, o teste falha. Um 1º experimento parecia "passar sem a proteção" porque o `Copy-Item` manteve a data antiga do arquivo restaurado e o ninja não recompilou; corrigido atualizando a data |
| [BAIXA] Falhas de `ConvertThreadToFiberEx`/`ConvertFiberToThread` não eram tratadas | **Aceito** | `IsThreadAFiber` antes de converter; falha na conversão é erro fatal; falha ao desconverter faz `sh4_threads_shutdown` devolver false, e o `GameHost` relata |
| [BAIXA] Uma segunda sessão herdava o hardware (VRAM, SPG com tempo antigo, Maple, GD-ROM) | **Aceito** | `boot_prepare` volta todo o hardware modelado ao reset. Teste `boot_prepare_resets_hardware_between_sessions` (VRAM limpa; VBlank na nova sessão mesmo depois de uma sessão com 5×10¹⁰ ciclos). Trace nativo e checkpoint continuam iguais |

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
- D7 decidido (Builder). Falta: Windows limpo sem Visual Studio instalado (só a mensagem
  de "sem compilador" foi testada, por vswhere inexistente); independência do Visual
  Studio (clang-cl + SDK); uma falha intermitente não identificada no `test_preview.ps1`.
- Vídeo NEWGAME.SFD termina cedo (hipótese: relógio do áudio, M6). Áudio e VMU.
- Mods de arquivo maiores que o original (realocar LBA + registro de diretório).
- Revisão do Codex desta etapa (limite de uso atingido em 2026-10-14).
- `--version` sem saída na verificação do Builder: intermitente, não reproduzido.
- clang-cl não está instalado nesta máquina. Por enquanto só o MSVC é testado.
