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
| **Port nativo** (`docs/PLANO-PORT-NATIVO.md` v2, aprovado 2026-10-15) | N0 e N1 concluídos. **N2 (tempo e eventos) concluído funcionalmente em 2026-10-24:** U1–U8 comprovados, espera de hardware correta no tempo lógico, experimental padrão na janela, `--tempo nativo` como recuo e headless estável por compatibilidade; N2c abre o N8. N3, N4, N5 e N6 concluídos; H9 do N6 aprovado. **N7 funcional, 95%, em fechamento técnico:** Vulkan padrão com recuo seguro; ainda bloqueiam o encerramento a causa não eliminada do DEVICE_LOST na AMD e a prova de fog/volumes contra o Flycast. O fallback é mitigação, não correção. **N8 iniciado em 2026-10-24: N8.0 (portão e referências) concluído em 2026-10-25; N8.1 (N2c) concluído em 2026-10-26 (tempo lógico sem ciclos nem TMU/INTC/CCN/porta; estável preservado); N8.2 (separação produto × validação) concluído em 2026-10-26; N8.3–N8.8 não iniciadas (o N8.3 só com autorização explícita).** Ver as seções de cada marco e o "Roadmap N0–N10". |

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

### N2b-2 experimental: tempo lógico sem ciclos estimados (2026-10-17, commit `8bfa94e`)

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

### Gameplay assistido: cobertura além da área 1, estável x experimental (2026-10-17, sem commit)

Relatório: `docs/N2b2-GAMEPLAY.md`. Teste: `tools/dist/test_assistido.ps1`.

**Infraestrutura (só desenvolvimento, sem emulação):**
- `BS_ASSISTIDO=<pasta>` com `--headless` (`app/assist.h`): o jogo pausa entre VBlanks e atende
  comandos (`pad`, `passos`, `quadro`, `ram`, `dump`, `roteiro`...); a sessão vira um roteiro;
- clientes `tools/dev/assistido.py` e `assistido_ir.py` (navegação até um ponto);
- roteiros indexados pelo **passo de jogo** (saídas da espera `8C064A2C`, uma por volta do laço
  principal), que valem igual nos dois modos de tempo; o índice por leitura do controle explicava o
  −26 do N2b-2 e um deslocamento de 6 passos já no START do título;
- `BS_STATE_PASSO` (estado lógico por passo) e coluna `passo` no `BS_FRAME_LOG`;
- `tools/dev/compara_modos.py`: comparação por passo.

**Alcançado:** 4 salas (`RCG00`, `RCG00A`, `RCG02`, `RCG03`), movimentação e câmera, portas
(evento, painel, porta trancada), menu e inventário, item encontrado e usado, combate com dano nos
dois lados, morte e GAME OVER, cenas e rádio, LOAD GAME e OPTIONS (sem VMU: "A VMU is not inserted").

**`test_assistido.ps1`: 28 casos, 0 falhas.** 4 roteiros, até 14 328 passos (29 mil VBlanks): quadros no fim de cada
passo, leituras, escritas na AICA (sem as cópias de guarda) e estado lógico a cada 10 passos
**iguais nos dois modos**; o negativo (entradas atrasadas 3 passos) é acusado. As únicas
diferenças são de tempo: boot (33 × 4 VBlanks), cargas de sala (5–9 × 2), vídeos (até +10).

**Verificado à parte:** sessão ao vivo = reprodução da `sessao.txt` (quadros linha a linha);
nenhuma espera nova nem registrador sem modelo novo nas áreas novas.

**Bloqueios:** porta C01 exige cartão de ID; monstro do heliporto não derrotado desarmado.

**Regressão completa** (2026-10-17, 11:23–13:38, sequencial), toda verde: builds release, debug,
mods e investigação; 213 unitários; ctest sem e com o disco 100%; `make_builder` e `make_preview`;
`test_preview` 9/0, `test_gameplay` 4/0, `test_builder` 18/0, `test_projeto` 13/0, `test_arquivos`
6/0, `test_ordem` 8/0, `test_tempo` 35/0, `test_n2b` 44/0, `test_cri` 33/0, `test_efeito` 17/0,
`test_n2b2` 17/0 (2 SKIP declarados; N11: estável byte a byte igual ao executável anterior ao
N2b-2), `test_assistido` 28/0.

### N3: investigação e plano dos controles nativos (2026-10-17, depois de `3a3fc92`, sem commit)

Autorização: **só investigação e planejamento**. Nenhum código mudou. Plano: `docs/N3-PLANO.md`.
Artefatos privados: `analysis/n3/` (dumps, comandos, hashes).

**Retomada depois do desligamento:**
- `master` limpo em `3a3fc92`, `git fsck` sem erros;
- builds de 11:06–11:23 (código congelado antes da regressão);
- Builder, Preview, backups `dist/*.assistido-3a3fc92`, `*.n2b2-8bfa94e`, `*.p0p1-246e6b3` e
  `analysis/rollback-antes-n2b2` presentes;
- a sonda interrompida só tinha o início do boot e foi refeita.

**Método:**
- sessão assistida (`passo_cgate` até o passo 7 972) com `BS_MAPLE_DEBUG`;
- 7 dumps de RAM com botões controlados e diferença por correlação;
- leituras `ram` com UP, ANALOG_LEFT e LT+RT;
- literais no `1ST_READ.BIN`, grafo de chamadas e disassembly.

**Medido:**
- Protocolo: DEVICE_INFO 1× na porta A, depois GET_CONDITION por VBlank; DEVICE_INFO nas portas
  vazias por VBlank; nenhum comando de VMU, LCD ou vibração.
- SDK `pd`:
  - servidor `8C15C4E6` (24 × `PDS_PERIPHERAL` de 0x34 em `8C2D8970`);
  - decodificador `8C15C24C`;
  - `pdGetPeripheral` `8C15C668`;
  - `old` vem da própria RAM.
- Jogo `8C064878`:
  - só a porta A; escolhe o controle pelas máscaras de `support`;
  - **escreve na estrutura do SDK** (zera sem controle, direcional → ±126, zona morta 20);
  - soft reset A+B+X+Y+START.
- Jogo `8C03AC2E`: 11 ações, limiar 48, auto-repetição.
- 28 funções do jogo usam o ponteiro `8C24CCB0`.
- `pdLcd` (`8C15E6D8`) e `bu` usam a mesma camada Maple (N4).
- Com o Maple sem dispositivos, o boot "VMU not found" é idêntico (14 quadros).
- As fronteiras "controles" `8C08B9E0/BA06/BA30` do N0 são *wrappers* de vídeo/VBlank, não de
  entrada.

**Plano:**
- fronteira no `pd` (slots no servidor e no getter; o init original continua);
- latch no ponto do disparo do DMA;
- `--controles nativo|legado`;
- subetapas N3-0 a N3-4, testes C1–C9, riscos e rollback.

**Revisão do Codex** (`reviews/codex-20261017-145645.md`): 1 ALTA, 4 MÉDIA e 2 BAIXA, todos
aceitos e incorporados (seção 8 do plano). O ALTA foi conferido no binário.

### N3-0: contrato, instrumentação e testes de referência dos controles (2026-10-17, sem commit)

Autorização: plano do N3 aprovado; **só o N3-0**. Decisões do mantenedor:
- SDL3 Gamepad, com XInput de reserva;
- remapeamento em TOML na pasta do usuário;
- no caminho nativo, sem controle virtual Maple e sem DMA de controle; o Maple fica só para a VMU
  até o N4;
- mouse fora do N3.

Relatório: `docs/N3-0-RELATORIO.md`. Suíte: `tools/dist/test_controles.ps1`. Artefatos: `analysis/n3/`.
Rollback: `analysis/rollback-antes-n3/` (executáveis de `3a3fc92` com hashes).

**Entrou (tudo desligado por padrão):**
- contrato puro do servidor `pd` (`transitional/pad_contract`);
- sonda que confere o servidor legado contra ele (`BS_PAD_CONTRATO`, build com mods), com mutações
  negativas e `BS_PAD_MODO`;
- traço por passo `BS_PAD_TRACE` e leituras `BS_PAD_LEITURAS`;
- nos roteiros: presença (`DESCONECTADO`), valores exatos (`AX`/`AY`/`A2X`/`A2Y`/`LT`/`RT=n`),
  `indice transicao` (transição injetada no primeiro acesso ao MDST depois do VBlank-In);
- mutações de traço `BS_PAD_MUTACAO`, `BS_MAPLE_FUNCAO`;
- ferramentas `pad_roteiros.py`, `pad_traco.py`, `converte_roteiro.py`, `pds_consumidores.py`;
- roteiros convertidos `passo_newgame_area1.txt` e `passo_newgame_exploracao.txt` (os originais
  ficam).
- Mudança de comportamento só em entrada de desenvolvimento: o segundo analógico vem da fonte de
  entrada (o padrão continua 0x80).

**Resultados:**
- **Contrato = legado** em todas as passadas observadas:
  - matriz com os 256 valores de R, L, x1, y1, x2, y2, 12 botões, 66 pares e os 4 padrões de `old`;
  - as 64 combinações de `support`;
  - modo 1;
  - gameplay, conexão e boot;
  - 4 rotas assistidas, cerca de 40 mil passadas.
- **Negativos acusados:**
  - contrato: A↔B, gatilho 127, `old` nativo, info sem conectado;
  - traço: A↔B, amostra adiantada um passo, amostra no VBlank-In com transição injetada.
- **Limiares do jogo**, nos 30 pontos de fronteira:
  - zona morta estrita `-20 < v < 20`;
  - direcional só com |v| > 48;
  - gatilho digital com byte ≥ 128.
- **Conexão:** desconectar com A, reconectar neutro e reconectar com A (borda no 1º passo), sem
  aviso na tela.
- **Campos não usados** (`r`, `l`, `x2`, `y2`, `old`, `name`, `extend`): com lixo, quadros, traço e
  estado do jogo a cada passo ficam iguais nas 4 rotas.
- **Roteiros convertidos** = originais (traço e quadros por VBlank).
- **Padrão inalterado:** quadros e leituras iguais ao executável de `3a3fc92` em `passo_cgate`,
  `passo_heliporto` e `newgame_area1`.

**Achados:**
- o jogo escreve na estrutura do SDK, e `old` vem da RAM;
- a tela de título consome a borda `press`;
- **soft reset A+B+X+Y+START chama o menu do BIOS (syscall em 8C001008), sem HLE: a execução
  termina com erro (legado, anterior ao N3)**;
- inventário de consumidores: 28 funções do jogo, conservador, não prova de fechamento.

**Revisão do Codex:** duas rodadas (`reviews/codex-20261017-164009.md`, `-173236.md`), todos os
achados aceitos (seção abaixo).

**Regressão completa:** com o executor novo retomável `tools/dev/regressao.ps1`; a primeira tentativa
foi interrompida no `test_n2b` por uma reinicialização da sessão e retomada sem refazer as etapas
aprovadas (fonte idêntica conferida). Resultado:
- builds release, debug, mods e investigação;
- 221 unitários; ctest sem e com o disco 100%;
- `make_builder`, `make_preview`;
- `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_arquivos`, `test_ordem`,
  `test_tempo`, `test_n2b`, `test_cri`, `test_efeito`, `test_n2b2`, `test_assistido` 28/0: todas
  verdes;
- `test_controles` 39/0.

**Percentuais estimados do roadmap** (peso no esforço total entre parênteses):
- N0 100% (5), N1 100% (4), N2a 100% (3), N2b ~85% (8), N3 ~25% (4: investigação, plano e N3-0);
- N4, N5, N6, N7a, N7b, N2c, N8 e N9: 0%;
- total ≈ 20%.

### N3-1: núcleo nativo de entrada (2026-10-17/18, sem commit)

Autorização: **só o N3-1**. Relatório: `docs/N3-1-RELATORIO.md`. Retomado em 2026-10-18 depois do
limite de sessão: o código gravado foi conferido (integração do toml++ no Builder e na exportação
já estava no disco), nenhum build ou processo pendente, nada descartado.

**Entrou:**
- `native/platform/input.h`, `input_map.cpp`, `input_system.cpp`: `ControllerState`, `ActionMap`
  (TOML via toml++, padrão, `--map` antigo), `InputSystem` (teclado, SDL3 Gamepad, XInput de
  reserva, escolha e troca de controle, conexão, zona morta radial opcional, latch);
- `transitional/input_bridge`: o Maple legado tira a amostra do núcleo e só traduz; conversões
  exatas nos 256 valores;
- `InputSource::connected`: os comandos do Maple que não leem o estado só perguntam a presença
  (uma amostra por GET_CONDITION);
- `app/main_sdl.cpp`: toda entrada pelo núcleo; `controles.toml` na pasta do usuário; foco solta
  só o teclado; diagnóstico `BS_CONTROLE_VIRTUAL` (controle virtual do SDL3);
- toml++ no produto (CMake raiz), no kit do Builder (`third_party/tomlplusplus`, licença) e no
  projeto exportado (`compilar.cmd` offline);
- `test_controles.ps1`: `-RotasK13`, e `-Casos`/`-RotasK13` aceitam lista com vírgula sob `-File`.

**Padrão inalterado:** o jogo continua no Maple legado (N3-2 não iniciado); VMU, escalonador e
tempo intocados.

**Verificado (método no relatório, seção 4):**
- builds limpos debug, release, mods e investigação;
- 229 unitários (+8); `ctest` sem e com o disco 100%;
- `make_builder`, `make_preview`, `test_builder` 18/0, `test_projeto` 13/0, `test_preview` 9/0,
  `test_gameplay` 4/0, `test_controles` 39/0, `test_assistido` 28/0;
- K13 (quadros e leituras iguais ao executável de `3a3fc92`): `passo_cgate`, `passo_heliporto`,
  `newgame_area1`, e também `passo_combate` e `passo_load_opcoes` (`-RotasK13`): as quatro rotas
  assistidas e a `newgame_area1`;
- projeto exportado compilado só com `compilar.cmd`, rede bloqueada: ok, toml++ local;
- janela: controle virtual do SDL3 e teclado real (`keybd_event`) chegam ao jogo com os valores
  esperados, inclusive desconexão com botão apertado, reconexão e remapeamento por
  `controles.toml`.

**Não verificado:** controle físico (nenhum conectado nesta máquina); fica o teste manual na
Preview.

**Percentuais estimados:** N3 ~45% (4); total ≈ 21%.

### N3-2: provedor nativo do SDK de controles (2026-10-18, depois de `e16e42f`, sem commit)

Autorização: N3-1 commitado em `e16e42f` (commit único autorizado) e **só o N3-2**. Relatório:
`docs/N3-2-RELATORIO.md`. Rollback: `analysis/rollback-n3-1/` (executáveis de `e16e42f` com hashes) e
`dist/BlueStinger-Preview.n3-1-e16e42f`.

**Entrou:**
- `--controles legado|nativo` (padrão `legado`), `GameOptions::controls`/`input`;
- `transitional/pad_adapter`: slots em `8C0975C0` (amostra no disparo efetivo do DMA da camada `per`;
  o original roda), `8C15C4E6` (servidor `pd` pelo contrato do N3-0) e `8C15C668` (getter); modelo de
  presença e coleta da camada `per`; info da porta A na área nativa do BIOS HLE;
- reserva formal `kHleNativeAreaBase = 0x8C004000` (0x100 B) em `runtime/hle/bios.h`, com guarda;
- `runtime/maple`: `maple_set_controller_present` (sem controle no nativo) e `MapleCounters`;
- `config/bluestinger_us.toml`: 3 slots; C++ regenerado (só os slots);
- diagnósticos `BS_PAD_FECHAMENTO` e `BS_PAD_NATIVO_MUTACAO`;
- `tools/dist/test_pd_nativo.ps1` (P1–P8, também no `tools/dev/regressao.ps1`); 5 unitários.

**Medido e corrigido no caminho:**
- `8C0975C0` volta sem disparar (−1/−2/−3): a amostra só com r0 ≥ 0;
- a camada `per` troca o **conteúdo** do buffer de info na coleta: ponteiro fixo, conteúdo pelo modelo;
- a coleta acontece no fim do DMA, que cai depois do servidor com tempo por ciclos e na espera seguinte
  com o experimental (dumps de RAM a cada VBlank em volta de uma desconexão): regra por modo.

**Verificado** (código final, builds limpos):
- 235 unitários; `ctest` com disco 100%;
- `test_pd_nativo` 76/0: legado = nativo nas 5 rotas e nos 6 cenários, nos dois modos de tempo
  (quadros, leituras, cargas, estado, áudio, traço com tempo), fechamento (0 entradas no servidor,
  decodificador e getter originais; 0 GET_CONDITION, respostas e leituras da fonte no Maple), 7
  negativos, determinismo, mods, área nativa e prova estática;
- `test_controles` 39/0 (K13: legado = `3a3fc92`), `test_assistido` 28/0;
- `make_builder`, `make_preview`, `test_builder` 18/0, `test_projeto` 13/0, `test_preview` 9/0, `test_gameplay` 4/0.

**Pendente:** teste manual com controles físicos (aceitação do N3 completo).

**Percentuais estimados:** N3 ~70% (4); total ≈ 22%.

### N3-3: recursos de controles do produto (2026-10-18/19, depois de `ba4fc2f`, sem commit)

N3-2 commitado em `ba4fc2f` (commit único autorizado); rollback `analysis/rollback-n3-2/`. Autorização:
**N3-3** e a regressão de fechamento do N3. Relatório: `docs/N3-3-RELATORIO.md`.

**Entrou:**
- perfis por GUID (`[perfis."<guid>"]`, `xinput`), com herança do `[controle]` e fallback;
- GUID do SDL3 e perfil no log de conexão e desconexão;
- documentação `docs/CONTROLES.md`, `LEIA-ME.txt` e exemplo comentado no arquivo padrão;
- mapa de teclado revisado e mantido;
- diagnóstico `BS_CONTROLE_FISICO` (com `DESCONECTADO`), `BS_CONTROLES_TOML`, `BS_CONTROLE_GUID`;
- `test_pd_nativo` P9; os negativos exigem execução sem erro e divergência real.

**Fora do escopo:** vibração (`pdVib` sem uso) e mouse (excluído no N3-0).

**Verificado:**
- 237 unitários;
- P9 7/7: roteiro físico pelo mapa padrão, pelo `[controle]` trocado e pelo perfil do GUID = roteiro
  lógico (traço com tempo e quadros, 9 558 passos); conexão com perfil = roteiro lógico com a porta
  neutra (9 498); perfil de outro GUID acusado;
- janela: GUID do SDL3 no log e perfil aplicado no caminho real.

**Pendente:** controles físicos Xbox e PlayStation (aceitação manual do N3), sem hardware nesta máquina.

### Fechamento do N3 e promoção do nativo a padrão (2026-10-19, sem commit)

**Regressão completa** (`tools/dev/regressao.ps1`, fonte `803FB171F8544023`, de 18/10 23:06 a 19/10
03:32, retomada sem refazer etapas aprovadas): **19 de 19 etapas, 0 falhas**. Cobriu builds,
unitários, `ctest` com e sem disco, Builder, Preview e as 16 suítes. Ela validou o código com o padrão
anterior (`legado`); a promoção foi aplicada depois.

**Promoção** (decisão do mantenedor; `docs/N3-3-RELATORIO.md`, seção 5): sem a opção,
`--controles nativo` passa a valer. O `--controles legado` fica como fallback temporário pelo Maple.
- Executável e `GameOptions`: padrão nativo.
- `test_controles`: K1–K12 fixados no legado, porque são as sondas do Maple. O K13 roda no padrão.
- `test_n2b2`: o N11, que compara ciclos com o executável anterior ao N2b-2, roda no legado.
- `test_pd_nativo`: P10 novo, para a execução sem a opção.

**Verificado depois da promoção:**
- builds release, debug, mods e watch;
- 245 unitários (237 do N3 e 8 do domínio de saves do N4);
- `ctest` com e sem disco;
- suítes afetadas, 0 falhas:

  | Suíte | Casos |
  |---|---|
  | `make_builder`, `make_preview` | ok |
  | `test_preview` | 9 |
  | `test_gameplay` | 4 |
  | `test_builder` | 18 |
  | `test_projeto` | 13 |
  | `test_arquivos` | 6 |
  | `test_tempo` | 35 |
  | `test_n2b2` | 17 |
  | `test_assistido` | 28 |
  | `test_controles` | 39 |
  | `test_pd_nativo` | 85 |

  - **K13:** o padrão novo dá quadros e leituras iguais aos do executável anterior ao N3.
  - **P10:** sem a opção, o fechamento indica nativo e os artefatos são iguais aos do nativo explícito.
- **Preview em janela, sem parâmetros:**
  - o controle virtual do SDL3 conecta, com o GUID no log;
  - START e A chegam ao PDS;
  - servidor `pd` original com 0 entradas e Maple sem controle.
- **Preview com `--controles legado`:** o caminho é o Maple, como antes.

**Pendente:**
- o teste manual com controle Xbox/PS físico, pelo roteiro em `docs/N3-3-RELATORIO.md`, seção 6. Não é
  declarado aprovado;
- Painel de Recomps: o antigo ficou sem acesso de edição nesta conta; em 2026-10-19 foi substituído
  por um painel novo desta conta, já com o estado do fechamento do N3 e do N4 em andamento.


### N4: oráculo SAVE/LOAD e provedor nativo dos saves (2026-10-19, em andamento)

**Checkpoint (2026-10-19, commit único autorizado com o fechamento do N3):** o N4 entra parcial. O
provedor nativo continua opt-in (`--saves nativo`; padrão `legado`) e **o SAVE/LOAD jogável segue em
investigação** (save do título lido como corrompido; falta um save de gameplay real). Rollback da
separação abandonada: `analysis/rollback-pre-commits-n3-n4/`.

Detalhes em `docs/N4-PREPARACAO.md`, seções "Oráculo SAVE/LOAD no Flycast: resultados" e "Piloto
Ghidra".

- **Oráculo (Flycast, VMU de trabalho; original intacta):**
  - o `emu.cfg` tinha `device1.1 = 10` (`MDT_None`); agora fica `1` (VMU) e `10` (`--sem-vmu`) só
    na sessão;
  - o boot detecta o cartão (inserção, mount, `disk_info`, 6 `exists = FF05`);
  - SAVE pelo título (H1 confirmada): `save` devolve 0, 11 consultas ocupadas, callback
    `(0x0C, 0)`, `status` 0;
  - VMS validado: CRC `BEDB`, payload = RAM;
  - persistência num processo novo: LOAD completo com payload em `+0x80`.
- **Provedor nativo (`--saves nativo`, opt-in; o padrão continua `legado`):**
  - `transitional/bu_machine` (protocolo sobre o `ISaveStore`) e `transitional/bu_adapter` (13
    slots e o servidor `8C08BDE8` registrado no agendador do jogo);
  - pasta `Documentos\DC-Recomp\Blue Stinger\saves`, `--saves-portatil`, `--saves-pasta`;
  - diagnóstico `BS_SAVES_H1=1` (build com mods).
- **Verificado:**
  - 250 unitários (5 novos da `bu`, com a sequência do oráculo como referência);
  - `ctest` com disco 100%;
  - `--saves legado` com quadros e leituras iguais ao executável anterior (`passo_load_opcoes`,
    `passo_cgate`);
  - no runtime nativo, o painel de SAVE mostra 6 slots, "Save completed" e o slot "ELIOT
    CLEARED";
  - o `BLUESTIN.000.bsv` tem o VMS **byte a byte igual ao do Flycast** (SHA-256 `a13a1266…`);
  - num processo novo, o LOAD GAME lista o slot e entrega à RAM os bytes do arquivo (`8C24F8BC` e
    `8C1F385C`), sem regravar o save.
- **Em aberto:**
  - o jogo diz "This save data is corrupt" ao carregar um save feito no título. A hipótese, não
    verificada, é que é a verificação do conteúdo pelo próprio jogo. Fecha com um save de gameplay
    real;
  - matriz de erros e repetição do oráculo;
  - revisão do Codex;
  - regressão de fechamento;
  - promoção a padrão.

### Roadmap: retorno ao N2 depois do N7 (2026-10-19, só documentação, sem commit)

Decisão do mantenedor registrada em `docs/PLANO-PORT-NATIVO.md`, seção 4.2. Ordem: N4 → N5 → N6 →
N7 → retorno ao N2 (pendências do N2b, inclusive U5–U8 e a avaliação da promoção do
`--tempo experimental`; depois o N2c) → N8 → N9. Sem N10. Antes do N8, o N2 precisa estar concluído
ou com os bloqueios documentados. Pontos de atrito registrados na seção 4.2: N5 e N6 validados nos
dois modos de tempo (a Opção C previa o N2b-2 antes deles), a referência de equivalência pós-N7 e a
posição do N7a. Percentuais inalterados: N2a 100%, N2b ~85%, N2c 0%, N2 ≈ 65%. Nenhum código nem
comportamento do relógio mudou; sem regressão (só documentação).

### N4: SAVE e LOAD de gameplay, Windows e Flycast (2026-10-19, depois de `b1c76ec`, sem commit)

Detalhes em `docs/N4-PREPARACAO.md`, seção "SAVE e LOAD de gameplay".
- **Causa do "corrupt":** o save do título (H1) não tem estado de gameplay. A máquina de LOAD do
  jogo só falha por timeout; a recusa vem da verificação do conteúdo.
- **SAVE de gameplay no RCG02** via `BS_SAVES_H2=<passo>`: só a presença do save point é simulada
  (kind 98, tipo 4 em `8C04FB80`). Resultado: "Save completed." e VMS válido (CRC `2835`).
- **LOAD num processo novo:** sem erro, a partida retoma na mesma sala e posição e continua jogável.
  - Windows: (126.9, 0.0, −560.4).
  - Flycast (`n4_vmu.py insere` + `bu-trace --ler-fim`): (126.85, 0.0, −560.42), destino do LOAD =
    VMS byte a byte, VMU intacta.
- **Modo espectador:** `BS_INPUT_SCRIPT` sem `--headless` abre a Preview com o roteiro como única
  entrada; o título mostra passo, sala e botões. Sem efeito no headless.
- **Verificado:** 250 unitários; headless com roteiro, 3 000 VBlanks.
- **Pendentes:**
  - save point real sem H2;
  - SAVE de gameplay no Flycast;
  - matriz de erros;
  - Codex;
  - regressão de fechamento;
  - promoção.

### N5: início em paralelo à regressão do N4 (2026-10-19, sem commit)

Autorizado em paralelo à regressão do N4. Plano em `docs/N5-PLANO.md`.

- **Isolamento.** O código fica na worktree `../recomp-BStinger-n5`, a partir de `b1c76ec`, com
  build próprio (`build
5`, `-j 2`). Nada nos arquivos que a regressão usa.
- **Inventário:**
  - 17 IDs na função de vídeo do jogo `8C08B544` (`r4` = ID, `r5` = controle); 16 SFD presentes e
    `OPENING.SFD` ausente no disco US;
  - todos os filmes são MPEG-1 320x240 a 30 fps com ADX estéreo a 44,1 kHz;
  - o áudio de eventos `E*.WAV` (`8C08B860`) fica para o N6.
- **Codex** (`reviews/codex-20261019-145826.md`): 9 achados, todos aceitos e incorporados ao plano:
  - ABI de 2 argumentos;
  - tabela de decisão de `r5`, flags, estados e interrupções;
  - as chamadas eram gráficas (alocador de 3 MiB, fade, texto), não de som;
  - V1 limitado aos filmes (a init do Sofdec fica para o WAV do N6);
  - regra de fim pelo O1;
  - relógio injetável;
  - duas referências de fidelidade;
  - matriz de testes;
  - 17 IDs e 16 recursos.
- **N5-1 (worktree):**
  - demux SFD e decodificador ADX (`builder/video`);
  - catálogo com mods e legendas, leitor e gravador MP4 por Media Foundation e reprodução pelo
    relógio lógico (`native/video`);
  - `bs-video-probe`.
- **Verificado:**
  - `bs_video_tests` 7/7, só com amostras sintéticas;
  - os 16 SFD reais com demux e ADX ok (quadros ÷ 30 = amostras ÷ 44 100 em todos);
  - **ADX idêntico bit a bit ao ffmpeg**, usado só como referência de desenvolvimento, em E017,
    E200A e NEWGAME (44 milhões de amostras). O probe achou um defeito: os coeficientes eram
    truncados e precisavam ser arredondados.
- **Pendentes:**
  - O1 (oráculo do contrato), em execução;
  - decisões D1–D5 do mantenedor (seção 7 do plano);
  - N5-3 e N5-4 depois do fechamento do N4.

### BlueStinger-Recompiler e BlueStinger-Developer; modo portátil no jogo (2026-10-20)

Pedido do mantenedor de 2026-10-19 (`docs/RECOMPILER-UI.md`). Feito na worktree
`../recomp-BStinger-ui` e integrado por patch em três vias sobre `bd7687a`. Conflitos resolvidos:
- `builder_core.{h,cpp}`: filmes do N5 mais o modo portátil;
- `builder/CMakeLists.txt`: `bs_video_convert` e `bs_native`;
- `test_builder.ps1`.

A renomeação `main_win32.cpp` → `main_developer.cpp` preservou o `--sem-filmes` do N5.

**Entregue:**
- **Dois executáveis sobre o mesmo `bs_builder_core`** (a CLI `bs-builder` fica):
  - `BlueStinger-Recompiler.exe`, a tela simples: CDI (com o seletor na pasta do Recompiler e um
    `.cdi` ao lado detectado sozinho), pasta, idioma, Modo portátil desmarcado por padrão,
    Recompilar, Cancelar, Abrir pasta, Jogar e Ajuda;
  - `BlueStinger-Developer.exe`, a janela antiga, com projeto C++ e log técnico.
- **Idioma:** lista suspensa (`CBS_DROPDOWNLIST`) com pt-BR e en-US.
  - Inicial: a preferência salva; sem ela, o idioma do Windows (pt-BR → português, o resto →
    inglês).
  - A troca retraduz a janela inteira na hora, inclusive o status já exibido.
  - Catálogo por chave (`builder/ui`); um idioma novo é um arquivo e uma linha.
- **Erros amigáveis por tipo** (`ErrorKind`): CDI inválido ou ausente, sem espaço, sem permissão,
  caminho longo, ocupado, compilador, dados, compilação, teste, instalação. O detalhe fica no
  `builder.log`.
- **Avisos antes de tocar numa instalação:** existente (saves e mods preservados), pasta alheia,
  troca de modo (as partidas não são transferidas e nada é apagado).
- **Modo portátil no jogo:** `instalacao.toml` (`native/platform/install_config`), gravado no
  pacote só quando a interface escolhe o modo e **fora da lista do pacote**: uma recompilação sem
  escolha (Developer, CLI) o preserva.
  - O `BlueStinger.exe` o lê ao iniciar.
  - Precedência: `--saves-pasta` > `--saves-portatil` > `instalacao.toml` > no `--headless`,
    cartão temporário > Documentos (`FOLDERID_Documents`).

**Revisão do Codex** (`reviews/codex-20261019-232955.md`): 4 achados, todos aceitos e corrigidos.

| Gravidade | Achado | Correção |
|---|---|---|
| MÉDIA | Falha ao arquivar a versão anterior **depois** da troca era mostrada como "nada foi alterado" | `ArchiveError`: sucesso com aviso (a nova está ativa; a anterior ficou numa pasta ao lado), na janela e na CLI |
| MÉDIA | Falta de espaço e de permissão perdidas no `instalacao.toml`, na criação do pacote e na troca | Código do sistema propagado (`write_config(..., code)`, `SwapError`, `GetLastError` do pacote). Teste da classificação; o teste achou que o MSVC equipara `ERROR_SHARING_VIOLATION` (o jogo aberto) a `permission_denied`, e `error_kind_of` passou a olhar os códigos do Windows primeiro |
| BAIXA | `instalacao.toml` inacessível virava Documentos sem aviso | Abertura e leitura conferidas, com aviso; teste com o arquivo travado |
| BAIXA | CDI ao lado não detectado com preferência antiga inválida | Detecção também quando a preferência salva não existe mais; `--disc` vence |

**Testes:**

| Teste | Resultado |
|---|---|
| `bs_ui_tests` | 17/17: catálogo completo nos dois idiomas, idioma inicial, troca, modos, precedência, erros e classificação |
| `ctest` | 14/14 |
| `test_saves` | **25/25**, com os novos S15–S18 |
| `test_builder` | **22/22** |

Casos novos do `test_saves`:
- S15: instalação portátil aberta direto carrega de `<instalação>\saves`;
- S16: movida para outra pasta, continua;
- S17: SAVE de gameplay grava na instalação;
- S18: precedência, sem o arquivo usa o cartão temporário, inválido avisa.

No `test_builder`, a etapa 11 roda o Recompiler com `--portatil --idioma en-US` e grava o modo; a
11b roda o Developer e o preserva. A janela foi conferida em captura, em pt-BR e en-US.

**Distribuição: bloqueada pelo compilador.** O Recompiler ainda exige o Visual Studio Build Tools
instalado pelo usuário. Opções e recomendação em `docs/DISTRIBUICAO-COMPILADOR.md`; decisão do
mantenedor pendente.

### N5: integração e primeiro filme nativo no jogo (2026-10-19)

As mudanças da worktree (`../recomp-BStinger-n5`, base `b1c76ec`) vieram para a árvore principal
por patch em três vias sobre o commit do N4 (`c5716f3`). Houve um só conflito, no texto de uso do
`main_sdl.cpp` (mantidos os dois lados). Plano e mapa em `docs/N5-INTEGRACAO.md`.

**O que entrou:**
- `--video nativo|legado` (padrão: **legado** enquanto o N5 está em validação) e `--legendas <idioma>`.
- Os 8 slots MW em `[native] slots`; código regenerado: 4 083 funções, 0 avisos.
- `bs_video_adapter` ligado ao `bs_app_core`; o `GameHost` instala os slots antes do boot, mostra o
  quadro do filme no lugar do quadro do PVR e imprime o resumo `[video]` no fim.
- No Builder:
  - os filmes são convertidos depois dos dados conferidos (`data\video`, manifesto dos 17 IDs),
    reaproveitados por SHA-256;
  - `--sem-filmes`, que também apaga os da compilação anterior;
  - `mods\video` do projeto vai para o pacote;
  - a licença do `pl_mpeg` vai em `licencas\`.

Com isso os dois achados ALTA da segunda revisão do Codex estão **resolvidos e validados no
executável final**.

**Primeiro filme nativo** (`tools/dev/n5_e001.ps1`, rota `passo_newgame_area1`, 7 300 VBlanks,
`--video legado` contra `--video nativo`): **9/9**.
- **Abertura:** o E001 abre pelo manifesto (ID 3) no passo 2138.
- **Sincronia:** fim natural em **13 713 ms lógicos, 411 quadros, 0 pulados**; 412 trocas de quadro
  em 828 VBlanks, com o quadro do filme na tela.
- **Nada do Sofdec:** nenhuma chamada aos originais do MW, entrada nunca exposta ao servidor por
  handle (`8C1588D8`); o nativo lê **0** setores do `E001.SFD`, o legado lê 599.
- **Volta ao gameplay:** depois do filme, **1 070 passos com quadros e estado lógico idênticos**
  ao legado, e as mesmas salas (`RCG00`, `RCG00A`).
- **NEWGAME não convertido nessa rota:** o estado de ausente (hipótese 4, PENDENTE O1b); o jogo
  segue.
- **Legado não serve de referência para a duração:** o filme do Sofdec emulado acaba em ~152
  VBlanks. O filme nativo toca inteiro e o jogo volta no mesmo passo.

**START durante o E001** (chamado com `ctl=01`): o filme vai até o fim natural, com controle
nativo e com controle legado.
- O laço do filme não lê o controle: lê uma palavra de estado do jogo (`@(r14+0x25AC)`, bits 4/8).
- Se o E001 é interrompível ali é **PENDENTE do O1b** (Flycast).
- O V8 do roteiro passou de afirmação (hipótese) a medida.

**Testes afetados** (bateria de 2026-10-19):

| Teste | Resultado |
|---|---|
| `ctest` | 13/13, com `video` (`bs_video_tests`, 36/36) |
| `n5_e001` | 9/9 |
| `test_preview` | ok, com os 16 MP4 na Preview (417 MB, manifesto dos 17 IDs) |
| `test_projeto` | ok |
| `test_gameplay` | 4/4 |
| `test_assistido` | 28/28 (os dois modos de tempo com os slots novos) |
| `test_builder` | **21/21**: 16 filmes convertidos, OPENING ausente, 17 IDs; na compilação seguinte, 0 convertidos e 16 reaproveitados por SHA-256; `--sem-filmes` tira os filmes do pacote |

As suítes do Builder passam `--sem-filmes`, fora o caso 13: converte, reaproveita e confere que
`--sem-filmes` tira os filmes do pacote.

**O N5 não está concluído.** Falta:
- **O1b:** cor base/offset, sondagens de preparação, regra de fim, estado de ausente, START;
- conversão e medida dos 16 filmes (matriz `tools/dev/n5_matriz.py --conversao`);
- as interfaces de decodificador e de rasterização de legendas (`docs/DECISAO-VULKAN-LINUX.md`,
  seção 3);
- promover `--video nativo` a padrão.

### N4: regressão de fechamento e encerramento (2026-10-19)

**Regressão completa** (`tools/dev/regressao.ps1 -Novo`, fonte `C29EFADBDD4305BD`, das 14:49 às 18:53):
24 etapas, 18 aprovadas.
- Builds, unitários (256/256), `ctest` com e sem disco (12/12 cada).
- Builder, Preview, `test_preview` 9/9, `test_gameplay` 4/4, `test_builder` 18/18, `test_projeto`
  13/13, `test_arquivos` 6/6, `test_ordem` 8/8.
- `test_assistido` 28/28, `test_pd_nativo` 85/85, `test_saves` 21/21 (S12, Flycast → Windows, pulado
  sem `-VmuFlycast`; ele foi provado à parte, em "Compatibilidade nos dois sentidos" do
  `docs/N4-PREPARACAO.md`).
- Depois do início, só mudaram scripts de teste e documentos: as etapas aprovadas valem para o
  executável final (`build/msvc-release`, 14:53).

**Seis reprovadas, todas em suítes de investigação estabelecidas noutro escopo:**

| Suíte | Falhas na regressão | Causa (A/B) | Reexecução |
|---|---|---|---|
| `test_tempo` | A3 (2) | estabelecida **sem cartão** ("VMU not found"); com o cartão nativo, o boot segue o código de cartão do jogo | `--saves legado`: **35/35** |
| `test_n2b` | B11 (2), S4 | idem (com cartão: 20 496 eventos contra 44 304 em 3 000 VBlanks; CRI em 1 923 VBlanks) | `--saves legado`: **44/44** |
| `test_n2b2` | N3, N3b, N8b | idem: quadros entre modos de tempo; o detector do N8b acerta a espera do CRI (`8C158860`), mas a thread passa a ser a 1, não a 3 | `--saves legado`: N3, N3b e N8b passam. O N11 falhou por erro do ajuste (`--saves` passado ao executável anterior ao N2b-2, que não conhece a opção); corrigido: reexecução completa: **17/17** |
| `test_controles` | K12 (2 passos de 3 470 e de 7 820) | roteiros por passo convertidos de execuções sem cartão: o START do passo 95 fecha o "VMU not found"; com o cartão não tem efeito. **Quadros idênticos** nas duas rotas | K12 com `--saves legado`: **aprovado** |
| `test_cri` | C2, B11b (com `--saves legado`: C2, C3, C4, B11b, sempre IRQF) | estabelecida **também com o controle pelo Maple**: o controle nativo virou padrão só no `b1c76ec`, e a regressão do N3 (fonte `803FB171…`) rodou com o legado. Com o nativo, o DMA do Maple termina antes e a ordem das interrupções muda sob as perturbações de tempo | A/B: executável anterior ao N4 (`e16e42f`) 33/33; atual com `--saves legado` 28/33; atual com `--controles legado --saves legado` **33/33** |
| `test_efeito` | E4, E6 (com `--saves legado`: E3, E4, E6, IRQF) | idem `test_cri` | `--controles legado --saves legado`: **17/17** |

**Nenhuma é defeito do produto.** O que deixa de valer com cartão e controle nativos são hipóteses de
investigação do N2b: D7 (perturbar só o tempo não muda a ordem lógica) e os controles só de tempo.
O código de cartão do jogo depende do tempo (como num Dreamcast com VMU), e a ordem das interrupções
do Maple muda com o controle nativo.
- Os saves nativos são determinísticos (`test_saves` S2, equivalência entre modos de tempo).
- Os controles nativos estão cobertos pelo `test_pd_nativo` (85/85) e pelo `test_assistido` (28/28).

**Decisão:** essas suítes rodam no escopo em que foram estabelecidas: `--saves legado` em
`test_tempo`, `test_n2b`, `test_n2b2` e K12, mais `--controles legado` em `test_cri` e
`test_efeito`, como já era o caso de K1–K13 e N11. Os critérios e as referências não mudaram.
**Pendência para o retorno ao N2:** reavaliar D7 e as perturbações só de tempo com cartão e controle
nativos (`docs/PLANO-PORT-NATIVO.md`).

**Encerramento do N4:**

| Critério | Situação |
|---|---|
| API `bu` atendida nativamente (13 fronteiras), sem VMU nem Maple | feito |
| Formato VMS e envelope `.bsv`, escrita atômica, recuperação, versões futuras, somente leitura | feito (unitários, `test_saves`) |
| SAVE → reinício → LOAD → gameplay no Windows | feito (`test_saves`, rota de gameplay) |
| Compatibilidade com o Flycast nos dois sentidos | feito (Windows → Flycast `g1`; Flycast → Windows `f2`/S12) |
| Matriz de erros | feita |
| Revisão do Codex | feita (rodadas do N4 nesta seção) |
| Promoção a padrão (`--saves nativo`; legado como fallback) | feita |
| Regressão de fechamento | feita, com as seis suítes reexecutadas acima |
| **SAVE num save point real** (ator kind 98) | **pendente**: as salas que as rotas alcançam não têm o ator; o local foi simulado (`BS_SAVES_H2`, `--forcar-save-point`). Mudar esse critério exige aprovação do mantenedor |

**O N4 está concluído, com a pendência formal do save point real declarada.** Não é aprovação
integral.


### N5: fechamento por testes dirigidos (2026-10-20)

Política de testes de 2026-10-20 (`CLAUDE.md`, nível 2): sem a regressão integral no fechamento das etapas.
Relatório: `docs/N5-RELATORIO.md`.

| Data | Item | Como foi verificado |
|---|---|---|
| 2026-10-20 | START durante o E001 no original (O1b-B, Flycast): o filme **não** é encurtado (fim na passada 8190, estado 3, `r0 = 0`, 1 937 iterações; 1 936 sem START). Nenhum fade (cor base e offset constantes) | `tools/dev/n5_oraculo.py`, rota do oráculo com UP desde 2630 e START em 6672–6690 (`analysis/n5/o1b`) |
| 2026-10-20 | START no nativo igual ao original: START dentro da janela (leitura 31 865 de 31 446–32 273), sem condição de saída, sem parada, mesmo fim (VBlank 32 293), retorno ao gameplay; linha do tempo até o START idêntica (31 884 registros de quadro). O legado não é referência (o Sofdec emulado encerra os filmes sozinho) | `tools/dev/n5_start.ps1` com veredito (`exit 1` em divergência): **rc = 0**, 1 438 s |
| 2026-10-20 | E001 no jogo com a cor do O1b ligada e o padrão novo | `tools/dev/n5_e001.ps1` **10/10**: V8 fim natural com START, V9 sem `--video` = `[video] nativo` |
| 2026-10-20 | Promoção: `--video nativo` padrão (`GameOptions` e argumentos), `--video legado` fallback até o N8; suítes históricas fixadas em `--video legado` (inclusive `test_controles` K1–K11 e o N12 do `test_n2b2`) | V9; revisão do Codex |
| 2026-10-20 | Builds e unitários depois das correções | ctest **15/15** (Debug) |
| 2026-10-20 | Regressão integral **ADIADA POR DECISÃO DO PROJETO** (mantenedor), não aprovada: 16 etapas aprovadas no mesmo código (builds, unitários, ctest com e sem disco, `make_builder`, `make_preview`, `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_arquivos`, `test_ordem`, `test_tempo`); `test_n2b` **cancelada** depois de 13 min (só a árvore de processos dela encerrada); não executadas: `test_cri`, `test_efeito`, `test_n2b2`, `test_assistido`, `test_controles`, `test_pd_nativo`, `test_saves`, `n5_e001` | `regressao.log` e `cancelada.txt` em `%LOCALAPPDATA%\BlueStingerRecomp
egressao` |
| 2026-10-20 | Defeito do executor achado: a chave depende da página de código do console que o lança (o `git diff` decodificado pela codificação do console); lançado do mesmo jeito, a chave das 16 etapas confere com a árvore | `-Listar` pelo bash (48865586EB109711) e por um PowerShell com `chcp 65001` (outra chave). Correção pendente (mudaria as chaves): fixar a codificação no início do executor |
| 2026-10-20 | Revisão do Codex do fechamento (`reviews/codex-20261020-115542.md`): 5 achados aceitos e corrigidos: `BS_RAM_WATCH` fora do `try` (unitário novo), `n5_start` sem código de saída, conversão passo → leitura (provada pela linha do tempo idêntica), suítes caindo no padrão nativo, falta de prova do padrão (V9) | testes acima |
### 2026-10-20: lote de build e testes parado 6,5 h (infraestrutura de testes)

| Data | Item | Como foi verificado |
|---|---|---|
| 2026-10-20 | Causa: `bs_unit_tests.exe` (Debug) parado numa janela modal "Debug Assertion Failed: vector subscript out of range" (0 s de CPU; ctest sem limite de tempo). Com `--video nativo` como padrão, o jogo falso de `test_app.cpp` instalava o provedor de vídeo nativo (sem o SDK MW), o host falhava e o teste indexava um quadro vazio. Correção (vai no commit de fechamento do N5): `fakeOptions` fixa `video = Legacy`, como já fazia com controles e saves | Texto da janela lido por UI Automation; reproduzido numa cópia (`game_host_publishes_frames_and_stops_at_limit` falha e depois vem o assert); build Debug isolado em `build\iso-debug` (configurar 5 s, compilar do zero 18 s) e ctest 13/13 (4 SKIP sem disco) em 41 s |
| 2026-10-20 | Testes sem janelas modais: `tests/sem_dialogos.cpp` (asserts do CRT de Debug e `abort` vão para o stderr; `SetErrorMode`) em todos os executáveis de teste, com o teste ctest `sem_dialogos` como prova permanente; limite de tempo por teste pela duração medida (10–40x; `boot_trace`, oráculo Flycast não medido aqui, 3600 s) e 900 s no preset para os demais; `regressao.ps1` com limite de tempo por etapa (rc=124, encerra a árvore de processos), saída gravada durante a etapa e batimento em `ativo.txt` a cada 30 s | Programa de prova: assert de vetor, `abort()` e falha de acesso terminam na hora (rc 127/3/139) com a mensagem no stderr; ctest Debug 14/14, `sem_dialogos` 0,06 s com a mensagem do assert no log (controle negativo: o mesmo assert sem a proteção é a janela que travou o lote); `test_regressao.ps1` 17/17 (R15–R17: etapa travada encerrada no limite, estado com rc=124, saída parcial no log, a retomada refaz a etapa) |
| 2026-10-20 | Onde vai o tempo: build incremental 2–5 s, Debug do zero 18 s. A regressão completa leva cerca de 4 h, quase toda em suítes de jogo sequenciais: `test_pd_nativo` 59 min, `test_controles` 43, `test_n2b` 40, `test_tempo` 23, `test_n2b2` 17, `test_assistido` 16 | `tools/dev/tempos.py` sobre o `regressao.log` (6 execuções) e o `.ninja_log` |

### BlueStinger-Developer completo: exportar, compilar e inspecionar (2026-10-20, worktree `developer-ui`)

Dois produtos sobre o mesmo `bs_builder_core`: o Recompiler (jogador: CDI → instalar → jogar, sem mudança de
comportamento) e o Developer (exportar o projeto C++20, compilar projetos modificados, inspecionar recursos).
Detalhes em `docs/RECOMPILER-UI.md`.

| Data | Item | Como foi verificado |
|---|---|---|
| 2026-10-20 | **Só exportar** (`Request::exportOnly`, `bs-builder --somente-exportar`, área *Exportar projeto*): valida, prepara e confere os recursos, recompila e exporta, **sem compilador, compilação nem instalação** | `test_projeto` caso 0: 13 s; o `builder.log` não tem as etapas de compilador, configuração, compilação, pacote, verificação nem instalação; sem `BlueStinger\` |
| 2026-10-20 | Projeto exportado com `CMakePresets.json` (release/debug, offline, mods, versão) igual ao `compilar.cmd`, `bs-projeto.toml` (pasta dos recursos) e README que diferencia recompilação estática de código-fonte original | `cmake --list-presets` e `cmake --preset release` no projeto (configura offline); `dev_export_project_files` |
| 2026-10-20 | Projeto só exportado, editado no C++ gerado, compila **sem o CDI** pela pasta dos recursos e a edição aparece no jogo | `test_projeto` caso 0 (`[edicao so exportar]` no boot) |
| 2026-10-20 | Release/Debug (`Request::buildType`, pasta de build por tipo), três áreas em pt-BR/en-US (72 chaves `dev.*`), inspeção de instalação e de projeto sem o CDI | `bs_ui_tests` (catálogo completo, inspeção); capturas das três áreas |
| 2026-10-20 | Revisão do Codex (`reviews/codex-20261020-135635.md`): 4 achados aceitos. **ALTA:** projeto de origem dentro de `.bs-build`/`BlueStinger` seria apagado ou trocado. Também: cancelamento durante a exportação, "recursos prontos" sem o contrato do Builder, versão ausente nos presets | testes novos `dev_project_inside_builder_folders_refused`, `dev_export_version_and_cancel`, `dev_inspect_exported_project` |
| 2026-10-20 | Resultado final, no código corrigido, contra o Builder montado numa pasta isolada (o `dist\` não foi tocado) | `bs_ui_tests` **24/24**; `test_projeto` **17/17** (418 s); `test_builder` **22/22** (574 s; Recompiler e Developer `--auto` e cancelamento). Pendente: abrir o projeto numa IDE (só o CMake foi provado) |

### N6 fases D e E: efeitos e falas pelo provedor nativo, contra o jogo original (2026-10-20; worktree n6-audio, sem commit)

**Método:** oráculo no Flycast (`tools/dev/n6_oraculo.py`, com âncora por RAM, analógico, `--trocar`, `--falas` e o
tempo do TMU0) na rota de combate convertida (`tools/dev/roteiros_n6/combate_flycast.txt`); comparação
`tools/dev/n6_compara_o6.py`; testes dirigidos `tools/dev/n6_efeitos.ps1` e `n6_falas.ps1` contra o legado.
Detalhes em `docs/N6-PLANO.md` seções 14–17.

- **O legado não é referência de som nem do fim das falas** (o driver de som e a posição da AICA nunca rodam): a
  afirmação anterior "o fim nativo cai na mesma passada do original" estava errada e foi corrigida.
- **Efeitos × original:** 107 `tocar` iguais (passo, porta, banco, pedido) e o +24 por porta igual. Corrigidos por
  evidência: o **nível base** por efeito (byte 0x19 do descritor; erro de −11 a +13 dB) e o **laço** (bit 1 → LPCTL; os
  7 efeitos tocavam uma vez). Mistura nativa do combate: pico −2,0 dBFS, 0 amostras saturadas. Pendente: o envelope da
  AICA (key-off antecipado do passo do Eliot, 6–15 VBlanks contra 15,2 da amostra).
- **Falas:** o mapa (239 IDs, 233 nomes = os 233 WAV do disco), o contrato do MW WAV e do player do jogo, e o
  provedor nativo (`--audio-falas nativo`, padrão legado). Medido no Flycast: preparo de 3 passos; estado 3 dois
  passos depois do fim do áudio. `n6_falas.ps1` **11/11**: `E012AENV`, `E012BENV` e a fala **`E002A`** (que o legado
  nunca toca), quadros iguais, estado igual ao legado até a divergência legítima (passo 2514). Dublagem de 30 s:
  estado do jogo igual ao sem dublagem.
- **FLAC:** `dr_flac` (domínio público/MIT-0, commit fixo, `third_party/dr_libs`). **OGG não integrado:** o
  `stb_vorbis` tem os CVE-2023-45675…45682 sem correção publicada (decisão do mantenedor).
- **Legendas:** `DialogueBridge` → `SubtitleService`; `--legendas` mostra o texto ativo no log; desenho no N7.
- **Unitários:** `bs_audio_tests` 40/40, `bs_localization_tests` 7/7 (normal e ASan); N0 `native/` limpo.
- **Codex** (`reviews/codex-20261020-170850.md`): 3 achados aceitos (estado só leitura com preparo por passo;
  orçamento de PCM do FLAC; pausa/cena expostas ao host), 1 rejeitado com evidência (os executáveis rodam todos os
  testes).
- Sem regressão integral (política de 2026-10-20).

### N6 fase F: música nativa (Manatee em C++) e a M2A no gameplay (2026-10-20; marco de integração, fase F ainda em validação)

**Método:** desmontagem ARM7 do `MANATEE.DRV` (Capstone 5.0.7, fora do repositório), oráculo no Flycast
(`tools/dev/n6_oraculo.py --musica --ca`, LOAD do cartão `g1_entrada`, 4 200 passos, sem a rota de ~50 min), comparação
um a um `tools/dev/n6_musica_compara.py`, rotas por passo no jogo (`passo_combate`, `passo_heliporto`, headless) e
unitários. Detalhes em `docs/N6-PLANO.md` seções 18.2–18.5.

- **Driver lido no código:** relógio de 250 Hz (Timer A 0xD4, 1 de 441 descartada = 1 000 Hz, ÷4); agenda das trilhas,
  `0x81` com N eventos (base no início do SMSD, 1 010/1 010), `0x82`, `0x84`; tom (FNS = floor(1024·(2^(j/1536) − 1)), as
  1 536 entradas); nível com a curva de volume 0x3EC8 (lida do `MANATEE.DRV` do usuário, versão conferida); pan e DISDL
  da camada; controles 7/10/11/32/64/121; fades da porta (`0x4D90`/`0x6640`); comandos em lote no tick, antes dos faders.
- **Nativo:** `native/audio/manatee` (SMSB/SMPB/tabelas), `manatee_seq` (sequenciador), `manatee_synth` (camadas, ADPCM,
  tom, nível, pan), `manatee_player` (slots, portas, faders), `envelope.h` (compartilhado, mínimo); `bs-musica`
  (ferramenta de desenvolvimento, WAV e eventos em `%LOCALAPPDATA%`).
- **Contra o Dreamcast (M2A):** key-on pareados **um a um** com mesma amostra, OCT, FNS, TL, DIPAN e DISDL: **5 590/5 766**
  em 50 ms e **5 757/5 766** em 100 ms (`bs-musica`, 150 s); **2 082/2 094 (99,4 %)** com a música tocada **dentro do jogo**
  (0 inversões; os 12 sem par no fade de entrada). Tempo: sem deriva entre 30 e 90 s; o Dreamcast atrasa até ~0,1 s nos
  trechos densos (**hipótese** de ticks perdidos pelo ARM7, mecanismo lido no código, sem medida direta; sem compensação).
- **Integração:** `--audio-musica nativo|legado` (padrão **legado**), 7 slots (`8C15D534` download — só SMPB/SMSB, o resto
  ao original —, `8C15DBAC`, `8C15DD40`, `8C15DCC0`, `8C15DC40`, `8C15D768`/`8C15D6A0` repassados), C++ regerado (só os
  slots mudaram). Na rota de combate: os comandos vêm do jogo recompilado na ordem do original; **estado do jogo igual ao
  legado nos 8 816 passos**; música audível (−22 a −34 dBFS RMS, 0 saturadas); no heliporto, os fades −47/−16/−113 do
  gerenciador seguem o algoritmo do driver (≈2 s com t = 2 000).
- **Codex** (`reviews/codex-20261020-203342.md`, 6 achados): aceitos e corrigidos com teste — limites do cabeçalho de
  tempo (overflow com dados ruins), chave do cache de amostras (SA + descritor + geração do slot), comparação um a um
  (antes por conjunto: os números antigos foram substituídos), auto-inserção no teste OGG, testes sem dados agora
  `[skip]` visível (`BS_EXIGIR_DADOS=1` os torna falha); aceito e **adiado**: pitch bend (só a M2C usa; a faixa do bend
  no driver não está provada). Também da revisão: a lista de 48 notas do driver.
- **Codex da integração** (`reviews/codex-20261020-220545.md`, 4 achados aceitos): comandos de música agora em fila por
  quadro, entregues no VBlank depois dos ticks (não valem mais para ticks anteriores à chamada; rota de combate conferida
  de novo); ids de nota contínuos por porta; duração real da soltura provisória (172 ms) documentada e testada; alegação
  do teste OGG restrita aos casos exercitados.
- **Música + falas nativas** (`passo_heliporto`, 13 312 passos): estado igual ao de só falas nativas e mesmos contadores
  das falas (6 create, 6 start, 3 861 consultas, 5 fins, 1 interrompida); fades −47/−16/−113 do gerenciador seguidos.
- **Tempo do download** (Flycast): o original fica ~177 ns/byte no `8C15D534` (85,5 ms para o SMPB de 483 680 bytes); o
  nativo devolve na hora: mesmo estado por passo, mas 1–2 VBlanks de diferença nas cargas. **Decisão do mantenedor: opção
  C**, transferido para o N2 (`docs/N2-PLANO.md` 11.1); não resolvido e não conta como fidelidade temporal.
- **Unitários:** `bs_audio_tests` **64/64**, normal e ASan, com `BS_GAME_DATA` (0 skips); sem os dados, 6 `[skip]`.
- **Pendências da fase F:** fade de entrada pela porta contra o Flycast na mesma rota; M2C (bend); as outras 10 músicas no
  jogo; ducking durante falas com música tocando; o envelope (fase G).
- Sem regressão integral (política de 2026-10-20).

### Legendas em inglês por padrão (2026-10-21; worktree n6-audio, sem commit)

**Método:** unitários e o jogo na rota `passo_combate` com o pacote de legendas por IA ligado numa pasta `mods` do build
de teste (não no `dist/`). Detalhes em `docs/LEGENDAS-EN-PLANO.md` seção 14.

- Padrão `en-US` (recua para `en`); `--legendas <idioma>|nenhuma` > `[legendas] idioma` no `instalacao.toml` > en-US.
  Filmes pelo renderizador do N5 e falas pelo `SubtitleService` do N6, sem código novo de apresentação; desenho das
  falas no gameplay continua no N7.
- Manifesto de mod com `provisorio = true` (o pacote por IA, não revisado): o jogo avisa no log; o pacote continua só
  em `%LOCALAPPDATA%` (fora do Git, do `dist/` e do Builder).
- Testes: `bs_ui_tests` 25/25 (`ui_install_config_subtitles`), `bs_localization_tests` 9/9
  (`loc_mod_provisional_and_en_us_default`). Jogo: sem opção, o filme NEWGAME mostra a 1ª legenda aos 143,2 s; com
  `nenhuma`, o mesmo quadro sem texto; estado por passo idêntico nas duas execuções (1 039 passos).

### N7c: efeitos internos e investigação da câmera livre (2026-10-22; worktree n7-vulkan)

**Efeitos de imagem novos** (Esc/Back > Gráficos > Efeitos de imagem; só a imagem do jogo, antes do menu e das
legendas):
- **tonemapping:** exposição em EV, compressão das altas luzes por um joelho suave, tom das sombras;
- **nitidez:** adaptativa ao contraste, sem halos;
- **bloom:** dois passes, limiar pelo canal mais forte para as luzes coloridas, raio relativo à altura;
- **CRT:** linhas de varredura nas 480 linhas do jogo, máscara de abertura e vinheta; só com resolução interna de
  1080p ou mais, decidido pela altura real da fonte. Abaixo disso o menu diz o motivo; filmes e telas da CPU ficam
  sem ele.

Cada efeito tem recurso próprio (`Features`), parâmetros persistidos em `graficos.toml` e liga e desliga na hora.
O preset Original continua sem nenhum passe. A cadeia ganhou passes internos por nome, binding 1 com a entrada do
passe anterior, três imagens em rodízio e timestamps de GPU por passe.

**Testes:**
- `bs_render_tests` 36 de 36:
  - por efeito: neutro igual; a +2 EV os pixels cortados caem de 4732 para 665 de 8192; o degrau 80|160 vira
    54|186 com as áreas lisas intactas; o bloom clareia em volta de um ponto (16 para 30) e não longe; no CRT, o
    centro da linha fica em 120 e o vão em 13;
  - a combinação dos seis passes;
  - o wrap dos timestamps.
- `bs_unit_tests` 289 de 289: faixas; `nan`/`inf` do TOML voltam ao padrão; o CRT desliga abaixo de 1080p com nota.
- **Janela com o jogo real** (`n7_janela.py`): cada efeito sozinho (tonemapping +1 EV: luma +35 %; nitidez; bloom;
  CRT em 1440p) e **todos juntos em 1080p, 1440p e 4K**: APROVADO, com as legendas idênticas (as mesmas amostras de
  texto) em todas.

| Passe (4K interno, RTX 5080, timestamps de GPU) | ms por quadro |
|---|---|
| correção de cor | 0,024 |
| tonemapping | 0,036 |
| nitidez | 0,037 |
| bloom (dois passes) | 0,063 + 0,069 |
| CRT | 0,024 |
| **todos juntos** | **0,280** |

Commits: `0ae1415` tonemapping, `875995c` nitidez, `898b087` bloom, `56fff7c` CRT, `2a4aea5` correções da revisão.

**Revisão Codex** (`reviews/codex-20261022-102720.md`), 4 achados, aceitos:

| Achado | Correção |
|---|---|
| [MÉDIA] CRT aplicado a fontes de 480 (filmes, telas da CPU) com a configuração em 1080p | decidido pela altura real da fonte ao gravar a cadeia |
| [MÉDIA] timestamps sem `timestampValidBits` (wrap) | diferença módulo 2^bits (teste) |
| [MÉDIA] `nan` do TOML atravessava o clamp até os shaders | não finitos voltam ao padrão, na leitura e no `clamp_to` (teste) |
| [BAIXA] o teste de custo passava sem timestamps | exige um tempo por passe quando a fila mede tempo |

**Câmera livre (investigação; proposta em `docs/N7-PLANO.md`, seção 23; aguarda a decisão do mantenedor):**
- Objeto câmera do SDK em `0x8C24CC38`: matriz de visão em `CC38`, posição em `CC78`, vetor de visão em `CC84`.
- Câmera do jogo em `0x8C24CD50`: posição `CD50`, pitch `CD5C`, yaw `CD60`, provável câmera de evento `CD68`.
- Montada por passo de jogo em `8C052240`: `8C1740DC`, `8C174148`, `8C174180`, `8C174308`, `8C1741EC`. A câmera fixa
  ou de evento vai por `8C078FC0`.
- Métodos: literal pools com `bs-disasm`, o registro de escritores novo (`BS_ESCRITOR`, só no build
  `-DBS_WRITE_WATCH=ON`) e o `BS_RAM_WATCH` na rota de combate.
- **Nenhuma mudança na câmera foi feita.**

**AMD:**
- `tools/dev/n7_validar_jogo.ps1`: o jogo com as camadas de validação (A1 AMD no modo comparar passando do render 371,
  N1 NVIDIA nativo, J1 janela com a apresentação direta).
- Recusa rodar em terminal elevado (conferido aqui).

**Validação com as camadas do Vulkan (2026-10-22; mantenedor, terminal não elevado; SDK 1.4.363.0, validação completa
+ sincronização, `report_flags` error,warn):**
- 1ª execução: logs vazios. O jogo é executável GUI e `& $Exe *> log` do PowerShell 5.1 não espera nem captura a
  saída. Corrigido no script: `cmd /c ... > log 2>&1` com `Start-Process -Wait`, como nas outras suítes.
- 2ª execução: camada ativa nos três casos e um único erro, `SYNC-HAZARD-WRITE-AFTER-WRITE` em `vkCmdEndRenderPass`
  (10 por caso, o teto da camada). O store da profundidade no fim do subpasso 1 (mesmo `DONT_CARE`, escrita em
  `LATE_FRAGMENT_TESTS`) não ficava ordenado depois da transição de layout da dependência 0→1. Correção em
  `native/render/vulkan/vk_device.cpp`: `LATE_FRAGMENT_TESTS` e `DEPTH_STENCIL_ATTACHMENT_WRITE` no destino da
  dependência (continua `BY_REGION`).
- 3ª execução, com a correção: **APROVADO**, 0 mensagens nos três casos.
  - A1, AMD Radeon integrada no modo comparar: 586 renders, 0 pixels diferentes, 0 recriações.
  - N1, NVIDIA nativo: 586 quadros, 0 falhas.
  - J1, janela 1440x1080 com a apresentação direta: 729 quadros, 0 falhas.
- Sem camadas, o modo comparar na NVIDIA continua com 0 pixels diferentes em 586 renders.
- **Pendência AMD continua aberta.** Com a correção e sem camadas, a rota de comparação na AMD passou 2 vezes e na
  3ª perdeu o dispositivo no render 371 (`vkQueueSubmit` -4, 1 recriação, diferença máxima 5). Com as camadas, nenhuma
  perda em 2 rotas. As camadas não acusam nenhum uso incorreto da API nessa rota, o que reforça a hipótese de falha
  do driver dependente do histórico da sessão. A correção da sincronização não é a causa nem a solução.

### N7: integração ao master, limpeza, build principal e validação (2026-10-22/23)

**Integração.**
- `master` avançou por fast-forward até o `n7-vulkan` (`317ded3`), sem conflito.
- As alterações locais particulares do principal ficaram onde estavam (`docs/DISTRIBUICAO-COMPILADOR.md`,
  `_bs-extract.exe`, `tools/extractor/build/`).
- Backups: branch `backup/master-antes-n7` e `%LOCALAPPDATA%\BlueStingerRecomp\backups\master-antes-n7-20261022`
  (patch local, arquivos particulares, `generated/` anterior).
- `generated/` regenerado com o `bs-recomp` do principal: idêntico ao do n7, 14 arquivos.

**`tools/extractor/known_versions.h` (particular).** A alteração local que acrescenta o Head Hunter apagou o `};` do
array, e o arquivo não compila (é o mesmo que entra no kit do Builder). Ele foi afastado com
`git stash push -- tools/extractor/known_versions.h` durante os builds e as distribuições e devolvido no fim, conferido
pelo SHA-256 (cópia também no backup acima). O conteúdo não foi alterado; a correção é do mantenedor.

**Achado do ctest.** `n0_native_limpo` reprovava: quatro comentários de `native/render` citavam caminhos de
`runtime/`. Os comentários foram trocados (`3298a92`). Release com o disco: 18/18 (`boot_trace` pulado).

**Limpeza (só o comprovadamente descartável, depois de procurar junções em todas as pastas):**
- `%TEMP%`: a instalação de teste do `test_preview` (1,1 GB), 8 pastas do `n7_janela.py` (0,6 GB) e diretórios
  menores de testes: 1,74 GB.
- Logs de build obsoletos na raiz das worktrees n6 e n7.
- `dist\`: as 8 cópias antigas da Preview (marcos N1 a N3), a pedido do mantenedor: 2,22 GB livres.
- Mantidos (candidatos, decisão do mantenedor): a worktree n6 (o `build/n7-ww` usa o `generated` dela); as capturas
  do N7 e do N6 em `%LOCALAPPDATA%` (a investigação AMD continua aberta); `dist\.bs-build`;
  `BlueStinger-Preview.anterior` (o rollback automático do `make_preview`).
- Junções existentes (não tocadas): `recomp-BStinger-n6\build\n6-game\app\mods\{legendas-en,video}`, para o pacote de
  legendas em `%LOCALAPPDATA%`.

**Build principal.** Preset `msvc-release` em `build\msvc-release`, com o SDL3 do `_deps` do principal e os
cabeçalhos Vulkan vendorizados (sem SDK). Distribuições:
- `dist\BlueStinger-Builder` (Recompiler, Developer, bs-builder, kit);
- `dist\BlueStinger-Preview` (montada pelo próprio Builder a partir do CDI).

**Validação da build principal** (Preview, cópia do executável em `%TEMP%`, 15 verificações; NVIDIA RTX 5080):

| Caso | Resultado |
|---|---|
| `test_preview` | 9/0 (inicia sem o CDI, dependências, pasta só leitura, sem `data\`) |
| Software headless (1200 VBlanks) | rc 0; áudio nativo (efeitos, falas, música) ativo |
| Vulkan headless | 586 quadros, 0 falhas, 0 recriações |
| Vulkan × software (comparar) | 586 renders, 0 pixels diferentes |
| Janela com apresentação direta: original, 1080p (1440×1080), 4K (2880×2160) | 0 falhas nos três; limite da GPU 2160 |
| Janela software (padrão) | rc 0, 59,9 campos/s |
| Shaders de mod | `cor-quente` válido; os 3 inválidos recusados com o motivo |
| Recuo sem Vulkan (janela e headless) | "Vulkan indisponível", segue no software |
| Menu pelo controle virtual: Legendas desligadas | `legendas.toml` gravado; o reinício lê "desligadas" |
| Câmera livre | estado do jogo idêntico ao Original. A primeira tentativa usou o VBlank 1200 e depois o 4000: até ali a rota de combate está em cena roteirizada (`CD68`), e a câmera fica corretamente suspensa |

**Distribuição pelo Recompiler.** O `BlueStinger-Recompiler.exe` de `dist\BlueStinger-Builder`, com `--auto --portatil`,
instalou a partir do CDI numa pasta nova: 16 filmes convertidos, mod padrão de legendas e jogo carregando o pacote
sozinho.

**Legendas como mod padrão** (decisão do mantenedor, 2026-10-22). O Builder instala em toda instalação os pacotes de
`mods-padrao/` (`2969913`). O LEIA-ME do jogo foi atualizado com os recursos do N7 (`b11179d`).
- **Atualização de 2026-10-23 (decisão do mantenedor):** as transcrições e traduções são texto derivado das falas do
  jogo comercial e não vão para o Git nem para o pacote público.
  - O conteúdo de `mods-padrao/` passou a ser só local (`.gitignore`; no Git fica só o `mods-padrao/README.md`, com
    o processo de geração).
  - O `make_builder.ps1` só os leva para o kit com `-LegendasLocais`, e o `VERSAO.txt` avisa: uso local, não publicar.
  - **Pendência para o mantenedor:** o commit `2969913` (local, sem push) contém o texto inglês 0.1.0 em
    `mods-padrao/legendas-en`. Antes de um push, esse commit precisa ser reescrito, e essa decisão é dele.

### Legendas: revisão, reconferência no áudio e pt-BR (2026-10-22/23)

Revisão por IA (Claude e Codex), sem revisão humana. Os dois pacotes continuam `provisorio = true`.

**Inglês (`legendas-en` 0.2.0):**
- **Revisão de texto:**
  - nomes pelo texto do jogo (o glossário passou a repetir até estabilizar: "Elliot G. Balad" → "Eliot G. Ballade";
    ESER, Kimratech, Gigadent Disk, Rat's Place, Dino-site);
  - pontuação dos diálogos;
  - contexto: Jean King (E080/E082C); Devlin; Doxin em vez de "dachshund" (E039, achado do Codex);
  - alucinações do Whisper em silêncio excluídas ("Thanks for watching!").
- **Reconferência no áudio** (`reconferir.py`, o mesmo modelo com os nomes do jogo), em 78 falas e 6 filmes:
  - 12 linhas recuperadas: os 9 avisos do E006D, tomados por repetição alucinada e confirmados com 0,97–1,00; o
    aviso curto do E101A; o E089B_G e o E089B_M;
  - correções de sentido: E067B, E056, E015, E102, E029B, NEWGAME 7;
  - a troca "Han" → "hon" (E048) foi desfeita: o áudio repete "Han", e a linha ficou "revisar".
- **Resultado:** 734 linhas no jogo (691 falas, 43 de filmes; eram 722 antes da reconferência): 609 automáticas, 53
  revisadas por IA, 72 ainda "revisar" (áudio duvidoso) e 11 excluídas.

**Português do Brasil (`legendas-pt-br` 0.1.0):**
- **Tradução por IA** das 734 linhas, com os mesmos tempos e chaves; duas linhas de 42 caracteres, conferidas pelo
  gerador.
- **Revisão independente pelo Codex de todas as 734 chaves**, em 9 lotes com uma linha por chave
  (`tools/dev/legendas/revisar_codex.py`; relatório em `reviews/legendas-pt-20261023-0108/relatorio.md`, local):
  - 667 ok e 67 com observação;
  - decisões: 33 aceitas, 5 ajustadas (o tamanho, ou a sugestão mudava o sentido), 5 rejeitadas com motivo (E125
    "trocar de roupa", E072B "preso", E185A "Gigadent" como lugar, E017B sem o "G.", E002B "Mal e mal");
  - **24 pendentes** de áudio ou contexto, mantidas sem alteração e listadas em `fontes/pendentes.pt-BR.tsv` no
    pacote: Stamp Rally, "Belliot", "OVA", "Han", sequências alfanuméricas e outras. Revisão ainda não concluída.

**Idioma.**
- O `instalacao.toml` e o padrão passam a ser `auto`: pt-BR se o pacote estiver instalado, senão en-US, senão
  desativadas com aviso.
- Menu Legendas > Idioma: Português (Brasil), English e Desativadas. Muda na hora e grava no `legendas.toml`.

**Validação na `dist`** (executável da Preview):
- execução direta: "idioma automatico: pt-BR (pacote legendas-pt-br)";
- **filme NEWGAME**: 67 quadros com pt-BR, en-US e nenhuma no mesmo VBlank. Em 19 há legenda, só na faixa de baixo
  e diferente do inglês; nos outros 48, nada sobre a imagem. Uma das capturas foi conferida a olho, com as duas linhas
  em português (o texto fica no pacote local, não aqui);
- gameplay e menu: ver "Validação da dist (2026-10-23)".

### Validação da dist (2026-10-23)

Executável e mods de `dist\BlueStinger-Preview` (`4c24049-dirty`, montada pelo Builder a partir do CDI, com os
pacotes locais `legendas-en` 0.2.0 e `legendas-pt-br` 0.1.0); casos em cópias da pasta, para a Preview não acumular
arquivos de teste; NVIDIA RTX 5080.

| Caso | Resultado |
|---|---|
| `test_preview` | 9/0: inicia sem o CDI, dependências, sem caminhos de desenvolvimento, pasta só leitura, sem `data\` |
| `test_saves` (`-Exe` da dist, cartões em pastas temporárias) | 25 ok, 0 falhas (S12 pulado: precisa de VMU do Flycast) |
| Execução direta da `dist` | "idioma automatico: pt-BR (pacote legendas-pt-br)"; filme NEWGAME pelo player nativo |
| Software / Vulkan / Vulkan × software | rc 0, áudio nativo / 586 quadros, 0 falhas / 0 pixels diferentes |
| Janela com apresentação direta: original, 1080p, 4K | 0 falhas; limite da GPU 2160 |
| Shaders de mod e recuo sem Vulkan | `cor-quente` válido e 3 inválidos recusados / segue no software |
| Câmera livre (VBlank 8860) | imagem diferente, estado do jogo idêntico ao Original |
| Menu > Legendas > Idioma (controle virtual) | pt-BR → Desativadas → English, gravado e lido no reinício (depois da correção abaixo) |
| Legendas pt-BR no gameplay (`n7_legendas.py`, software e Vulkan) | APROVADO nos dois: 866 quadros com fala, 339 sem |
| Legendas no filme NEWGAME (pt-BR × en-US × nenhuma) | 19 quadros com legenda só na faixa de baixo, diferente do inglês; 48 sem nada |
| Movimento relativo (R0, R3, R7, R10) | R7 rumo 92,7° / vista 92,8°; R10 acompanha o giro; R3 idêntico ao Original |

**Defeito achado na validação da dist (corrigido).** O menu oferecia só pt-BR e Desativadas: com os dois pacotes
instalados, a lista do menu tinha um idioma. A causa era
`for (... : ModPackages::discover(...).packages())`: no C++20 o temporário de `discover()` morre antes do laço, e o
laço lia uma referência pendente (comportamento indefinido). O resultado agora fica numa variável, e o log mostra a
lista ("[menu] idiomas das legendas: en-US, pt-BR") e os pacotes recusados com o motivo. Um unitário novo reproduz o
caminho da dist (pt-BR automático → Desativadas → English). Não há outro laço com esse padrão no código.

### Fechamento do N7 (2026-10-23)

Decisão do mantenedor (2026-10-23): Vulkan preferencial com recuo seguro para o software; a pendência AMD continua
registrada; SSAO, widescreen real e MSAA passam para o N9. Detalhes: `docs/N7-PLANO.md` seção 25.

- **Vulkan padrão da janela** (`kRenderNativeDefault = true`, `GraphicsSettings::vulkan = true`); `graficos.toml`,
  `--render` e `BS_RENDER` valem sobre o padrão; o `--headless` continua no software (referência das baterias).
- **Recuo:** sem dispositivo, `clamp_to` força o software (antes herdava o padrão da estrutura); apresentação direta
  com falha recua para o SDL; perda do dispositivo no jogo: 3 recriações e depois o software até o fim da sessão.
- **Defeito corrigido:** a troca de predefinição no menu trocava o renderizador; agora o preserva.
- **Validação** (build Release, RTX 5080), método: `valida_fecho.ps1` (pasta de teste com DLLs e mods da Preview):
  F1 janela sem opções no Vulkan (428 quadros, 0 falhas); F2 sem dispositivo no software (rc 0); F3 `graficos.toml`
  com software respeitado; F4 `--render software`; F5 headless no software; F6 comparar 586 renders, 0,0000 %.
  Unitários 304/0, UI 25/0.
- **Revisão do Codex** (inline, `reviews/codex-20261023-093452.md`): 1 achado [MÉDIA] rejeitado (o Vulkan no headless
  com `--render nativo` é intencional, caso C2 das baterias); a ajuda da linha de comando foi esclarecida.
- **Legendas:** pendências (72 en-US a revisar, 24 pt-BR dependentes do áudio, só as chaves) e a verificação dos
  direitos de redistribuição em `docs/LEGENDAS-PENDENCIAS.md`: não há autorização para publicar o texto; os pacotes
  continuam locais, instalados pelo Builder na distribuição de testes. `LEGAL.md` passou a listá-los entre o que nunca
  entra no repositório. Dois trechos de uma linha do texto do jogo (um no `STATUS.md`, um num unitário) foram trocados
  por descrição e texto próprio.
- **Publicação:** a limpeza descrita em `docs/PUBLICACAO-GITHUB.md` foi executada antes da publicação de 2026-10-23;
  em 2026-10-24 o fechamento do N2 foi publicado por avanço normal até `0903ba5`, depois de nova varredura de 898
  frases comerciais em 8 commits, com 0 achados e os arquivos particulares preservados.

### N2 retomado: desempenho e H9 (2026-10-23)

Método: rota LOAD (save g1), Preview `465b2ed`, RTX 5080; headless 6 000 VBlanks em duas rodadas alternadas e a janela
pelo `n6_h.ps1 -Casos H9` (150 s). Detalhes: `docs/N2-PLANO.md` seção 12 e `docs/N6-PLANO.md` 20.6.

| Medida | Resultado |
|---|---|
| Trabalho por campo, headless 9 000 VBlanks com LOAD real (software / Vulkan, tempo estável) | 16,63 / 6,82 ms |
| Idem, tempo experimental | 13,99 / 4,04 ms |
| H9 na janela, Vulkan padrão (3 rodadas) | **0 faltas** em ~8 977 blocos; 7,14–7,22 ms por campo |
| H9 na janela, software (mesmo executável) | 287 faltas; 17,11 ms por campo |
| H9 a 4K com bloom e tonemapping (LOAD real) | 2 faltas; 7,92 ms por campo (espera síncrona da GPU por render) |
| H9 com tempo experimental e Vulkan (LOAD real) | 1 falta; 4,50 ms por campo (cargas num campo só, pico de 62 ms) |

- **Correção de método:** uma primeira matriz headless e as variantes 4K/experimental copiaram o save sem importar (o LOAD não acontecia); foram refeitas com o LOAD real (valores acima). O H9 pelo `n6_h.ps1` não foi afetado.
- **Causa das faltas do H9:** o rasterizador em software (~10 ms por campo nesta rota), não o áudio nem o modelo de tempo. Com o
  Vulkan padrão o H9 passa e o **N6 fica concluído**.
- **Tentativa revertida:** trocar o `vkQueueWaitIdle` depois da cerca por um carimbo do quadro (para a thread do jogo
  não esperar a apresentação) não melhorou (480p: 8,22 ms por campo; 4K: igual, com um pico de 2 s). Código voltou ao
  anterior; unitários e render passaram nas duas versões (304/0, 36/0).
- **Pendências:** espera síncrona da GPU (leitura assíncrona: decisão de arquitetura); recuo para o software abaixo do
  tempo real; 11.1 (download da música); promoção do experimental (U5–U8).

### N2 11.1: espera do DMA do download da música; passos de jogo no tempo legado (2026-10-23)

Detalhes e provas: `docs/N2-PLANO.md` 12.4 e 12.5.
- Espera do DMA (176,7 ns/byte) no download nativo da música, pela primitiva `time::hardware_wait` (no experimental não espera, D11). Sessão gravada do mantenedor (7 496 passos, 4 trocas de música): estado idêntico em todos os passos e saves iguais, com e sem a espera; VBlanks 6–26 mais tarde (o tempo das cargas). Revisão do Codex: 2 achados aceitos, 2 rejeitados com evidência.
- Defeito corrigido: `game_steps()` era 0 no `--tempo legado`, e as falas nativas (por passo) faziam legado e nativo divergirem com o áudio nativo. `test_tempo`: 22 → 34 de 35; o A3 restante era a referência do N1, registrada com o áudio legado (atualizada).
- Pendente: `test_assistido` A5 (três roteiros param no passo ~1037 nos dois modos de tempo; a investigar). Resolvido
  na seção seguinte.

### N2: A5, áudio a 4K e cobertura do tempo experimental (2026-10-23)

Detalhes e tabelas: `docs/N2-PLANO.md` seção 13.
- **A5 (`d6b0e45`):** os roteiros por passo foram gravados com o vídeo legado. Com o vídeo nativo, o filme do NEW GAME
  (452 s lógicos) toca inteiro dentro do passo 1038 e o orçamento do `--headless` acabava nele. Correção:
  `test_assistido` com `--video legado`, como o `test_tempo`. Resultado: 28/0, A5 até o fim das quatro rotas.
- **Áudio a 4K:** faltas raras (0–2 em 150 s a 4K e 1440p; 0 a 480p) vêm de picos de GPU no título e nos menus
  (65–78 ms de GPU, medido com o diagnóstico novo `BS_VK_LENTOS`). No gameplay nenhum render passou de 19 ms.
  **A sincronização assíncrona da GPU não foi implementada:** não é comprovadamente necessária, porque nos picos a
  GPU está de fato ocupada.
- **Cobertura do experimental:** `tools/dev/n2_sessoes.py` reproduz as sessões gravadas e confere o estado por passo
  e os saves. A lista fechada `config/n2_excecoes.toml` tem 5 exceções D11; a regra `d11-carga-cartao` é amarrada ao
  registro de LOADs do jogo (`BS_SAVES_LOG`, `transitional/bu_adapter.cpp`).

| Prova | Resultado |
|---|---|
| estável, 2 sessões (7 915 + 7 496 passos) | igualdade estrita em todos os passos; saves iguais |
| experimental, 2 sessões | 1 passo diferente em cada (1703, 1084), cada um logo depois de um LOAD (1702, 1083) e igual no seguinte; saves iguais |
| negativo: mutação de RAM (vida, `8C24FBCC`) no experimental | FALHA (6 001 passos e saves diferentes) |
| negativo: registro de LOADs ignorado (`--negativo-sem-loads`) | as 2 sessões rejeitadas só pela falta do LOAD |
| negativos da lista fechada | id repetido, regra repetida, campo faltando e regra desconhecida: recusados |

- **Promoção do experimental:** **não recomendada ainda** (13.4). Pendências: 1 falta no H9; a espera do DMA da
  música ausente (D11); U5, U6 e U8; U7 parcial (rotas cobertas, U1–U4 não repetidos nelas).
- **Revisões do Codex:**
  - `codex-20261023-182322`: 5 achados, todos aceitos e corrigidos (LOAD amarrado ao passo; rodapé ausente e
    referência vazia falham; só LOADs até o fim; o TOML controla o verificador).
  - `codex-20261023-184053`: 4 achados, todos aceitos e corrigidos (`--reavaliar` com `--mutacao` recusado; negativo
    exige diferenças; ids e regras únicos; referência de 1 até o fim).
  - `codex-20261023-184000` não vale como revisão: a cópia do repositório recusou o git (dono do diretório), o Codex
    não recebeu o diff e não emitiu achados. Por isso a revisão foi refeita com o material no prompt.

### N2: conclusão funcional e promoção do tempo experimental (2026-10-24)

Detalhes, critérios U1–U8 e limitações: `docs/N2-PLANO.md` seção 14.

- **Defeito funcional corrigido:** a espera do DMA da música agora anda o instante lógico no experimental, entrega
  VBlank/render/Maple/GD-ROM no caminho e tem guarda de término. Com isso, os lotes do driver preservam a ordem e a
  carga não fica concentrada num campo. A exceção `d11-download-musica` foi removida.
- **Padrão do produto:** janela em tempo experimental; `--tempo nativo` é o recuo. Headless continua no estável para
  não invalidar baterias, referências e gravações antigas. `--tempo` explícito prevalece.
- **Gravações:** novas sessões guardam o modo explicitamente; sessões antigas sem `--tempo` reproduzem em
  `--tempo nativo`. O comparador sempre põe o modo pedido por último. As duas sessões do mantenedor passam nos dois
  modos; no experimental, somente um passo intermediário depois de LOAD difere, pela regra fechada, e os saves são
  iguais.
- **U1–U8:** aprovados nas rotas cobertas. A rota ampliada inclui troca de sala, combate, SAVE e LOAD; o detector teve
  0 violações, negativos foram detectados e arquivos/controles legados deram o mesmo estado e saves dos nativos.
- **H9 experimental:** 3 execuções de 150 s na janela, 0 faltas de áudio; 4,06–4,10 ms por campo; pior campo 40–43 ms.
- **Validação de fechamento:** unitários 306/306; sessões antigas em estável e experimental; evidências aprovadas de
  `test_assistido` 28/28, `test_tempo` 34/35 com a única referência histórica explicada, U2–U5 e H9 reutilizadas.
- **Repetição final:** sessões estáveis com 7 915 + 7 496 passos, 0 diferenças e saves iguais; experimentais com só
  os passos 1703 e 1084 pós-LOAD diferentes, reconvergência imediata e saves iguais; pausa/retomada 6/6 na janela
  padrão experimental; CTest Release com 14 aprovados, 3 skips esperados e 0 falhas (17 casos; `real_disc` excluído
  pela alteração particular incompleta de `known_versions.h`, preservada).
- **Distribuições atualizadas:** Preview `5ce94b5-dirty` aprovada em 9/9; Builder aprovado em 22/22, incluindo
  Recompiler, Developer, compilação completa e incremental, rollback, 16 filmes e reuso pelo SHA-256. O sufixo
  `dirty` vem somente dos arquivos particulares preservados; nada foi publicado.
- **Limites não escondidos:** diferença isolada de um passo pós-LOAD; inventário U1 limitado às rotas cobertas;
  diferença de TN só no Sofdec legado; fallback em software abaixo do tempo real; picos raros de GPU em menus a
  1440p/4K. N2c e a ampliação para a campanha inteira entram no N8; otimizações opcionais ficam depois dele.

### N7c: câmera livre em ambientes fechados (2026-10-23)

Detalhes, método e tabelas: `docs/N7-PLANO.md` 23.4.
- **Causas comprovadas:** (1) a ancoragem da vista parava quando a câmera do jogo a alcançava; daí em diante a vista seguia a câmera de perseguição, que gira atrás do Eliot (no corredor, segurando "direita" ele andava em círculo); (2) o toque do analógico esquerdo só acabava com zero exato (zona morta 0; parado dá ~0,003), e o desvio de cortes antigos ficava (RMT00: 104°, 0 de 42 direções certas). O jogo usa a mesma referência (`D680`) nas áreas abertas e fechadas.
- **Correção:** vista sempre ancorada no Livre; toque acaba abaixo de 0,05; linha de base invalidada depois de cena, suspensão e troca de modo (revisão do Codex, 2 achados aceitos). Modo Híbrido retirado (`hibrida` → Livre).
- **Validação:** 7 áreas (aberta, corredores, sala fechada, combate em corredor, sala após o externo, transição interno → externo), 8 direções × 5 ângulos: orientação certa em todos os segmentos em que o Eliot anda; Original idêntico ao executável anterior; combate idêntico ao Original; Vulkan = software; gravação × reprodução iguais. Câmera originalmente fixa: nenhuma zona nas rotas disponíveis (pendente).

### Legendas das falas: sincronia e separação (2026-10-23)

Detalhes, causas e medições: `docs/N7-PLANO.md` 18.6; pendências e direitos: `docs/LEGENDAS-PENDENCIAS.md`.
- **Causas comprovadas:** tempos do ASR esticados sobre o silêncio (72 linhas > 0,5 s antes da voz, até 12 s na
  reprodução); linhas nunca divididas nas pausas (48 cobriam duas falas); no jogo, o tempo do mixer em vez do audível
  (fila da saída XAudio2 de 133–216 ms) e o tempo negativo arredondado para 0.
- **Correções:** pacotes en 0.3.0 e pt-BR 0.2.0 ressincronizados pela voz audível, com partes nas pausas, sem mudar
  texto nem chaves (locais, fora do Git); no jogo, legenda pelo quadro que sai do dispositivo, ciclo próprio por
  ocorrência e parte, troca de idioma no meio da fala, `BS_LEGENDA_LOG`, opção "Identificação do personagem"
  (indisponível: nenhum pacote diz quem fala).
- **Medições:** sessão gravada, entrada − início audível de −7 065…+103 ms (12/17 antecipadas) para −49…0 ms (0);
  na janela, a legenda entra 150 ms depois do início no mixer, com a fila medida de 133–150 ms; 0 quadros com falas
  diferentes juntas.
- **Testes:** unitários 304/0, áudio 80/0, localização 10/0; `n7_legendas` APROVADO em software e Vulkan (com o
  executável novo e com o da `dist`); filme NEWGAME pt-BR/en/nenhuma sem problemas; troca de idioma e pausa na janela.
- **Revisão do Codex:** 2 achados aceitos (descartes de bloco na fila; log e numeração na troca de idioma).
- **Dist:** Preview e Builder `8b73b3a` com os pacotes novos (Preview `mods\`, kit `mods-padrao\`); o LEIA-ME do jogo
  com o texto novo das legendas entra na próxima montagem.

### Correções de jogo e interface: menu, câmera, legendas dos filmes, ícone e cursor (2026-10-23)

Pedido do mantenedor: cinco defeitos vistos no jogo, ícone e cursor. Cada item tem teste de regressão próprio.

| Item | Causa e correção | Prova |
|---|---|---|
| Menu: jogo pausado depois de mudar uma opção (`a690d58`) | fechar com B/Esc na página principal ou "Voltar ao jogo" fechava só o modelo; só o View/Back retomava o jogo. O menu agora guarda a pausa que fez e retoma por qualquer caminho | `tools/dev/n7_menu_pausa.py` (janela, controle virtual; vídeo, áudio, controles, legendas, isolados e em sequência): executável anterior 2 de 3 falham; novo 6/6 |
| Menu de vídeo simplificado e idioma da interface (`03b00cf`) | sem "Predefinição"; "Efeitos de imagem" vira submenu sem valor; indicador › nos submenus; interface no idioma das legendas (pt-* = português, os outros = inglês; desativadas mantêm o último; vale no reinício; restaurar padrões mantém o idioma) | unitários (submenus, idioma), capturas em pt-BR e en-US |
| Ícone (`9bc9b65`) | ícone original do projeto no git (`app/res`, desenho geométrico); o da capa do jogo só local (`icone-local/`, fora do git, `make_builder.ps1 -IconeLocal`), sem grão e com nitidez por tamanho | o ícone extraído do executável; `check_kit` confere o `.ico` (vazio, truncado, válido) |
| Cursor (`b24b4ae`) | escondido só em janela, com foco e o menu fechado; visível no menu, sem foco e em tela cheia; visível ao sair | `tools/dev/n7_cursor.py`: 8 estados na ordem, com o estado real do SDL |
| Câmera Livre na troca de cenário | o desvio dos cortes somava o salto da vista da troca (~134°); a carga de sala (`gdfs::room_loads`) vira corte e zera o desvio, sem soltar o analógico | `tools/dev/n7_camera_transicao.py`: 105° → 0°; Original sem mudança (5 817 passos idênticos). `docs/N7-PLANO.md` 23.5 |
| Legendas dos filmes (N5) | a ressincronização usava a energia, e a trilha dos filmes tem música o tempo todo: linhas esticadas até a seguinte e falas juntas. Os filmes passam a usar as palavras do ASR; no jogo, a legenda segue o áudio audível (monotônico: tirou uma piscada medida no NEWGAME) | `tools/dev/n5_legendas_filmes.py`: 5 de 6 filmes com defeito → 0, en e pt-BR; NEWGAME na janela, en e pt-BR: cada linha uma vez, 0 a +0,2 s depois da voz. `docs/LEGENDAS-PENDENCIAS.md` "Filmes" |

**Dist (2026-10-23, `98206d3`; "-dirty" = os dois arquivos particulares, fora dos commits):** Preview e Builder
remontados (Builder com `-LegendasLocais -IconeLocal`: uso local, não publicar). No executável distribuído (gerado pelo
próprio Builder): ícone da capa embutido; `n7_menu_pausa` 6/6; `n7_cursor` aprovado (a primeira tentativa não conseguiu
o foco do Windows para a janela de teste e a regra respondeu "sem foco"; a repetição obteve o foco e passou);
`n7_camera_transicao` aprovado; pacotes 0.3.1/0.2.1 em `mods\video` com os 12 filmes×idiomas aprovados. O kit do
Builder leva o ícone local e não leva o `known_versions.h` particular. Sem regressão do gameplay: Original idêntico em
`passo_combate` (5 817 passos) e as legendas das falas idênticas byte a byte; sem baterias longas.

Revisões do Codex: `codex-20261023-193911` (menu: 1 aceito, 1 rejeitado com evidência, o rótulo "ANALOGICO" nunca
aparece), `codex-20261023-195145` (ícone e cursor: 3 aceitos), `codex-20261023-212005` (câmera e filmes: 6 aceitos, 1
registrado como limite: o provedor legado de arquivos não conta as cargas de sala).

### N9: planejamento oficial (2026-10-23; N9 não iniciado)

Pedido do mantenedor: consolidar o planejamento do N9 (Blue Stinger Remastered), só documentação. Registro em
`docs/N9-PLANO.md`; referências em `docs/PLANO-PORT-NATIVO.md` (seção 4), `ARCHITECTURE.md` (seção 6 "N9 (planejado)"
e D14), `docs/RECOMPILER-UI.md` ("Futuro") e `CLAUDE.md` (regras permanentes). Decisões de escopo:
- **um projeto, duas edições instaláveis** do mesmo núcleo C++ (Original e Remastered), escolhidas pelo Recompiler por
  um **perfil de edição** separado das preferências; sem fork nem branch por edição;
- **Remastered:** câmera Livre obrigatória, legendas ligadas em pt-BR, menus e saves de PC (VMU invisível ao jogador,
  adaptador interno mantido), gráficos modernos ajustáveis, widescreen; **Original:** fiel ao Dreamcast, validada
  contra as referências do port;
- subetapas N9.0–N9.11 com requisitos, critérios de aceitação, prioridades (E/R/O) e decisões abertas (12);
- começa depois do N8; a ordem N0–N10 não muda; ferramentas e APIs de mods continuam no N10.

### N8: preparação técnica completa (2026-10-24; somente documentação)

O N8 continua **não iniciado (0%)**. `docs/N8-PLANO.md` foi confrontado com os alvos CMake, includes e o mapa Release:
o produto ainda liga o runtime híbrido completo, enquanto CDI, Builder/Recompiler, Flycast e ferramentas são
dependências de construção/validação e não devem ser confundidos com legado obrigatório de runtime.

- inventário L1–L22 registra caminho/símbolo, consumidor, substituto, evidência, destino, risco e teste;
- N2c foi incorporado como N8.1, a primeira subetapa técnica; N8.0 apenas congela referências;
- sequência N8.0–N8.8 separa tempo, alvos, boot/arquivos, Maple, filmes, AICA, render e fechamento em cortes reversíveis;
- software permanece como fallback de produto, mas deve virar backend da IR antes da retirada de PVR/TA/VRAM;
- a pendência AMD impede eliminar o fallback; fog por tabela/modifier volumes impedem retirar a referência gráfica;
- N7 em 95% não bloqueia N8.0–N8.6: as referências continuam intactas; fog/volumes bloqueiam N8.7 e AMD mantém o N7
  aberto sem transformar recuperação em correção;
- decisões antes da implementação: limite N8 × elevação N10, fonte da IR, forma do alvo validation e campanha como
  portão de fechamento. Nenhum código, comportamento, dist ou etapa N8/N9/N10 foi iniciado.

Revisão Codex `codex-20261024-180744`: dois achados médios aceitos. O portão N7→N8 passou a ser por subetapa e N2c
passou a enumerar o núcleo temporal; DMAC/SQ e os módulos de PVR, Maple e GD-ROM ficam para seus cortes posteriores.
Segunda revisão `codex-20261024-180905`: três achados médios aceitos; separou `sh4_intc` de Holly, tirou DMAC da
remoção física do N8.1, alinhou a segunda definição do N2c e deixou D-N8-2 como portão somente do N8.7.

### N8.0: portão e referências (2026-10-24/25; sem mudança funcional de jogo)

Autorização do N8 e decisões D-N8-1 a D-N8-4 em 2026-10-24 (`docs/N8-PLANO.md` seção 9). Diretrizes do N8.1 aprovadas
no mesmo dia, para depois do N8.0 (`docs/N8-1-PREPARACAO.md` seção 4.1). Nada do N8.1 foi implementado; `--tempo
nativo`, `BS_POLL`, `BS_CYCLES`, INTC, TMU, CCN e o gerador não mudaram.

**Regressão nível 3** (`tools/dev/regressao.ps1`, árvore `5ce94b5`, terminada em 2026-10-25 02:48, 16 942 s): 25
etapas, 16 aprovadas e 9 reprovadas. Causas e destino:

| Etapa | Causa | Destino |
|---|---|---|
| `ctest`, `ctest_disco` | o validador SPIR-V do pós-processamento formava `&w[i + 1]` numa instrução final sem operandos (`OpFunctionEnd`): assert de span no Debug | corrigido (`w.data() + i + 1`), teste do `OpFunctionEnd` final, `bs_render_tests` sem diálogos e com TIMEOUT; ctest Debug 18/18 com e sem disco |
| `test_projeto` | RC2135: o `.rc` com o caminho acentuado do ícone | o `.ico` vai para o binary dir e o `.rc` o cita pelo nome; `tools/dev/icone_exe.py` confere o ícone dentro do exe (RT_GROUP_ICON/RT_ICON = `.ico`) no caminho acentuado; `test_projeto` 18/18, `test_builder` 22/22, `test_preview` 9/9 (a primeira execução reprovou só a `--version`, saída vazia logo depois do build, sem o código no detalhe; não reproduziu sozinha: provável tempo esgotado de 30 s, não comprovado) |
| `test_controles` (K12; K13 abortado) | K13: `--video legado` passado ao executável de referência anterior ao N5, que imprimia o uso e saía; K12: roteiros convertidos de execuções com o áudio legado | K13 corrigido (autorizado): a referência sem `--video`, `--audio legado` só nos jobs do K13; K12: A/B abaixo |
| `test_pd_nativo` (P3 conexão/limiares, P9 negativo) | o orçamento de VBlanks não tinha o filme NEWGAME nativo (27 146 VBlanks dentro do passo 1038): 43 execuções paravam no meio do filme e as comparações "iguais" passavam no vazio | +27 200 VBlanks (vídeo nativo mantido, autorizado); o mesmo motivo deixava a matriz no passo 2161 de 2249 (os passos 2161–2249 nunca eram comparados); limiares e conexão: 9 323 / 9 263 passos, 0 diferenças |
| `test_n2b`, `test_cri`, `test_efeito`, `test_n2b2` | áudio nativo como padrão (A/B abaixo) | fixação aprovada (opção b) e aplicada; suítes aprovadas (abaixo) |

**Guardas contra falso positivo** (autorizadas; `test_controles` K5, controle do K10, K11, K12, K13 e
`test_pd_nativo`): uma comparação de igualdade só vale com as duas execuções completas: rc 0, o orçamento inteiro,
sem linha `erro:`, deadlock contado ou interrupção nunca reconhecida; todo log do job com dados; quadros, traço e
estado no último índice do roteiro; o roteiro presente; dois artefatos vazios nunca são iguais. Os contadores normais
(`deadlock 0`, as faltas de progresso do modo sombra no `--tempo nativo`) não reprovam. Prova offline sobre as
execuções gravadas: 44 aprovadas aceitas, 15 negativos (vazio, só cabeçalho, truncado, roteiro ausente) e as
execuções truncadas da regressão rejeitados. Execução real com as guardas finais: `test_controles -Casos K11,K13`
12/12 e `test_pd_nativo -Casos P3` 32/32 (antes, K5/K10/K11/K13 19/19 e o `test_pd_nativo` completo 81/86, as 5
falhas da matriz pelo orçamento, corrigido e reexecutado). Os passos por VBlank com o vídeo nativo e o legado diferem (9 323 × 9 554
na rota de limiares): contagens de passos entre os dois não provam equivalência passo a passo.

**A/B do áudio** (mesmos executáveis, dados e referências; só `BS_AUDIO=legado`; nenhuma das 81 saídas do A/B com
`[audio] nativo`):

| Suíte | Nativo | Legado | 19–20/10 |
|---|---|---|---|
| `test_n2b` | 38/44 | 44/44 | 44/44 |
| `test_cri` | 29/33 | 33/33 | 33/33 |
| `test_efeito` | 16/17 | 17/17 | 17/17 |
| `test_n2b2` | 13/17 | 17/17 | 17/17 |
| `test_controles` K12 | 0/2 | 2/2 | aprovado |

Primeira divergência por caso:
- **específicos do legado:** C2, C5 e A1 (sem stream ADX nem escritas na AICA com o áudio nativo); N3b (constante de
  alinhamento de 26 leituras medida no legado; o N3c passa); S4 (limiar de cobertura); K12, N11 e K13 (roteiros e
  executáveis de referência do legado);
- **rótulo ou limiar do teste, critério mantido:** N5 e N8b (no experimental com o áudio nativo a thread do CRI nasce
  primeiro e vira a 1, não a 3; identificando a thread pela entrada, 99,94%); S2 (um episódio de espera aberto no fim
  da rota, sem saída sem evento nem deadlock); B4 (só o provedor de arquivos muda uma fusão);
- **diferença real em modo de diagnóstico (TMU parado):** B11 (VBlank 2114, uma IRQ `0x320` antes da `0x360`) e E6
  (VBlank 2095, uma troca a mais para a thread do CRI). É a mesma limitação da hipótese D7 registrada em 19–20/10 com
  o cartão e o controle nativos. **Pendência do N8.1** (medidores do CRI e TMU).

Nenhuma regressão funcional do áudio nativo.

**Fixação do áudio aprovada (opção b, mantenedor, 2026-10-25):** `--audio legado` só nos 4 jobs do K12, no lado novo do
N11 e nas suítes `test_cri` e `test_efeito` (inteiras); no `test_n2b` e no `test_n2b2`, duplicatas `*_al` com o áudio
legado só para o que reprova no nativo (S2 da espera do VBlank, B4 fundidos, B11, a cobertura do S4, N3b), com as
execuções atuais no nativo para todo o resto; N5 e N8b identificam a thread pela função de entrada (evento `N`), com
o critério de 99% mantido. Nenhum limiar, referência ou asserção foi afrouxado. Execução real, em sequência (o B9 mede
desempenho), com os executáveis da regressão:

| Suíte | Resultado |
|---|---|
| `test_controles -Casos K12` | 2/2 (area1 3 470 passos, expl 7 820, 0 diferenças) |
| `test_cri` | 33/33 |
| `test_efeito` | 17/17 (E7 SKIP, como antes) |
| `test_n2b2` | 17/17 (2 SKIP, como antes); refeito depois da revisão final, com `Ended` exigindo rc 0 e o N9 com dados: 17/17 |
| `test_n2b` | 52/52 (44 + 5 execuções duplicadas terminadas + S2 legado ×2 + negativo do S4 no nativo) |

Três classes de resultado, daqui em diante:
- **obrigatórios no áudio nativo:** todo o `test_n2b` menos os quatro abaixo (inclusive S2 da espera do render e o
  negativo do S4, que passam no nativo e continuam obrigatórios nele), N1–N10 e N13 do `test_n2b2` (N3b à parte), as
  suítes `test_tempo`, `test_assistido`, `test_saves`, `test_pd_nativo`, a bateria do N6 (`n6_h.ps1`, H9) e as 16
  referências do N8.0;
- **históricos (áudio legado):** K12, N11, `test_cri`, `test_efeito`, e S2 (VBlank), B4 fundidos, B11, S4 e N3b nas
  duplicatas;
- **informativos (`[info]`, não aprovam nem reprovam):** o resultado nativo desses cinco: S2 VBlank (3 789 de 3 790
  episódios com saída, sem evento 0, deadlock 0: a rota termina dentro de uma espera), B4 fundidos 14/14/15/15, B11
  difere (D7 com o TMU parado, pendência do N8.1), S4 chamadas sombra = legado com 2 933 VBlanks com passadas, N3b
  difere (alinhamento fixo medido no legado).

**Cobertura nativa do que virou informativo:**

| Verificação | Comportamento nativo que deixou de ser critério | Cobertura obrigatória que continua no nativo | Lacuna |
|---|---|---|---|
| S2 (espera do VBlank) | toda saída da espera `8C064A2C` com VBlank-In entregue, no modo sombra | S2 da espera do render, B7 (0 deadlocks), S3, B1/B2 (TN, IRQ, IRQF tempo legado = sombra), `test_tempo` A1/A2 | baixa: o contador direto "sem evento = 0" (proposta S2n abaixo) |
| B4 (fundidos) | fusões iguais entre os dois provedores de **arquivos** | B4 colapsados 0, B4 arquivos legado sombra = tempo legado, B2 IRQF, negativo B4 | nenhuma: dentro de cada provedor os tempos concordam (14 = 14, 15 = 15); o provedor legado de arquivos sai no N8 |
| B11 | hipótese D7 com o TMU parado | `test_tempo` A5 (leituras de TMU), `test_n2b2` N7 (timeout de render) | não é produto (modo de diagnóstico); a diferença real é pendência do N8.1 |
| S4 | chamadas do servidor do CRI por VBlank, sombra = legado | negativo S4 no nativo, B1 TN, B6 SL/quadro/AUD | pequena (proposta S4n abaixo) |
| N3b | quadros iguais por ponto lógico, estável × experimental | `test_assistido` A1/A4/A7 (por passo, 4 rotas), N3c, as 16 referências | nenhuma: o índice por passo substitui o alinhamento fixo |

`test_cri` e `test_efeito` inteiros no legado (decisão aprovada): os casos que passavam no nativo (C1, C3, C4, A1
positivos, B11a–c, E1–E5) passavam sem stream ADX, quase no vazio. O mecanismo que eles sustentam no nativo segue em
`test_n2b2` N1 (a thread do CRI bloqueia no experimental), N8b, N9 (detector necessário) e N10, e nas referências; a
equivalência por passada do bloqueio do CRI com o áudio nativo (C3/E3) é requisito da validação dos slots do CRI no
N8.1 (`docs/N8-1-PREPARACAO.md`).

**Propostas de testes nativos (não implementadas, aguardam o mantenedor):**
- **S2n:** na espera do VBlank do modo sombra, com o áudio nativo: "sem evento" = 0, "saídas com evento" = liberações
  por VBlank-In, e episódios − saídas ≤ 1 só se o fim da execução cair dentro de uma espera. Mesmo critério semântico,
  contando saídas em vez de episódios;
- **S4n:** chamadas do CRI por VBlank, sombra = legado, com o áudio nativo, exigindo as duas execuções completas e o
  traço até o último VBlank no lugar da contagem de VBlanks com passadas (calibrada nos streams do legado). É critério
  novo, com justificativa própria, não o limiar antigo afrouxado.

**Revisões Codex da fixação:**
- `codex-20261025-110619`: N5 aprovava dois traces só com cabeçalho (100%). **Aceito**: as duas execuções completas e
  16 001 assinaturas por lado; prova offline (aprovados aceitos; só cabeçalho, truncado e incompleto rejeitados). Os
  outros dois achados são sobre `known_versions.h`, arquivo particular fora do git: fora do escopo;
- `codex-20261025-133623` (final): (1) `test_cri`/`test_efeito` inteiros no legado tiram do nativo casos que passavam:
  **rejeitado como mudança de código** (decisão aprovada pelo mantenedor), **aceito como documentação**: o mapeamento
  acima; (2) `Ended` do `test_n2b2` ignorava o rc: **aceito**, agora exige rc 0; (3) o negativo N9 aprovava um mutante
  sem artefatos: **aceito**, exige as duas execuções completas e com dados. Prova offline 9/9.

**Referências do N8.0** (`tools/dev/n8_referencias.py`, especificação e resultado em `docs/N8-0-REFERENCIAS.md`): o
harness novo (validação por caso, comparação estrita de `(nível, SPC)` e da ordem `I/T/N/R/J` por intervalo de VBlank,
exceções só explícitas e por diferença, cartão inicial bruto e normalizado, configuração única, conjunto dos 16) com
41 testes unitários. Captura inválida anterior guardada em `analysis/n8/descartada/`. Piloto aprovado (0 diferenças
contra ele mesmo, negativo acusado). **16 casos** capturados com um único executável (`exe_sha256 d05b9014…`), todos
válidos; reprodutibilidade independente (`conferir` estrito, o mesmo executável) dos **16**: 4 em 2026-10-25 de manhã e os
12 restantes à tarde, 0 diferenças em todos os artefatos, inclusive `eventos` e `relogios`; negativo `BS_RAM_MUTACAO`
acusado. A matriz do `test_pd_nativo` (5 falhas da execução completa pelo orçamento) foi revalidada pelo P3 corrigido
duas vezes: execuções completas 28/28, matriz nativo 2 603 e experimental 2 621 passos com 0 diferenças, fechamento
da matriz nos dois modos.

Revisão Codex `codex-20261025-081520` (6 achados, todos aceitos e corrigidos com teste ou prova): conjunto dos 16
obrigatório numa captura completa; estado exatamente 1..N na ordem; cada exceção de SPC consumida por uma diferença;
guardas das suítes com dados e último índice em todo artefato comparado; documentação sem exceção de posição (o
comparador reprova todo deslocamento); esta seção. A prova da correção 4 achou dois defeitos dela própria (um `-1`
literal que o PowerShell passa como texto; roteiro ausente aceitando), corrigidos antes de qualquer execução.

### N8.1: fim da simulação de tempo (N2c), concluído (2026-10-25/26)

Autorizado pelo mantenedor em 2026-10-25, só o N8.1. Detalhes em `docs/N8-1-PREPARACAO.md` seção 7.

Continuam como estão:
- `--tempo nativo`, o padrão do headless e as gravações antigas (D-N8.1-A);
- o gerador e as macros;
- as referências do N8.0.

**S2n e S4n** (commit `5964379`):
- `tools/dev/n8_nativo.py` (29 testes) e `tools/dist/test_n8_nativo.ps1`;
- referência pré-migração em `analysis/n8/pre-n8-1`, 17/17;
- S2n: 0 saídas sem evento na espera do VBlank, nos dois modos;
- S4n: 2 008 064 chamadas do CRI iguais entre o tempo legado e o modo sombra, negativo acusado.

**Instrumentação `BS_INTC_TRACE`** (commit `b571ab3`; tipos H/A/C/L/E/S, num fluxo separado, no eixo de ordem do
`BS_EVENT_TRACE`):
- as 16 referências do N8.0 ficam iguais com o trace desligado e ligado;
- no experimental, 0 entregas e 0 efeitos pelo quantum de ciclos nos 8 casos;
- revisão Codex `codex-20261025-162709`:

| Achado | Decisão |
|---|---|
| S4n: negativo truncado aceito | aceito: o negativo exige trace completo e menos chamadas |
| registro S com efeitos de outras fibras | aceito como documentação: o registro só pode contar a mais, então S = 0 continua prova |
| origem aberta depois de fibra apagada | aceito: zerada no reset |
| trace de chamadas sem marcador de fim | rejeitado: um rodapé quebraria a leitura do `test_n2b`; as duas execuções terminam com rc 0 e flush, e são comparadas entre si e com a referência |

- duas leituras de ciclos na instrumentação subiram a catraca `tempo.consumidores.ciclos` (0 → 2, ctest Debug): tiradas;
  catraca de volta a 0.

**Cadência B** (no tempo lógico):
- o atendimento só roda quando pedido, e a vivacidade vai por um tick a cada 4 096 polls;
- o estável continua pelo quantum de ciclos;
- correção aprovada: a espera do hardware restaura o ponto de retomada antes de cada atendimento.

Contra as 16 referências:
- estável: 8/8 idêntico;
- experimental: idêntico fora de 179 SPCs, todos nas esperas do hardware;
- as 179 estão em `config/n8_1_excecoes_spc.toml`, uma por diferença e conferidas uma a uma por
  `tools/dev/n8_excecoes_cadencia.py` (7 testes negativos).

Opção "com correção" aprovada pelo mantenedor em 2026-10-25, com a condição de cada uma ser provada inofensiva:
- mesma espera, sem `T`/`J`/`N` no caminho, todas as entregas com origem `hardware`;
- eventos seguintes iguais (81 com o mesmo `rte` desviado para `8C17DE4C`, 98 sem nenhum);
- o primeiro estado depois igual;
- o SPC só passa por `stc.l`/`ldc.l spc` no tratador `8C1982F4` antes do `rte`.

Prova de independência dos ciclos (`BS_CICLOS_RUIDO=5000`): experimental 8/8 iguais com as mesmas 179; estável 0/8
(controle).

| Teste | Resultado |
|---|---|
| unitários | 309/309 (2 testes novos da cadência B) |
| ctest Debug | 18/18, com e sem disco |
| `test_n2b2` | 17/17: N8a/b/c pela checagem nova; N12 experimental 21,31 ms × estável 25,10 ms |
| `test_n8_nativo` | 17/17: S2n e S4n iguais a referencia pre-migracao |
| `test_assistido` | 28/28 (experimental x estavel por passo nas 4 rotas) |

Revisões Codex:
- `codex-20261025-185027`: 2 achados baixos aceitos, com o contexto registrado no boot e testes unitários do protocolo;
- `codex-20261025-202021`: 1 achado baixo aceito, a "mesma espera" provada pela origem `hardware` e a ausência de
  `T`/`J`/`N`, com o texto do gerador separado do que o comparador prova.

**TMU, CCN não gráfico, cabo e INTC no tempo lógico** (2026-10-25/26):
- interceptação MMIO aprovada pelo mantenedor; detalhes em `docs/N8-1-PREPARACAO.md` 7.4–7.7;
- o código recompilado do CRI e o gerador não mudam, e o estável continua nos modelos.

| Grupo | Commit | Prova |
|---|---|---|
| TMU (`memory/tmu_logical`) | `1abbf6a` | 16/16 com as 179; leituras do TMU iguais nos 16 casos; 0 fora do contrato; ruído 8/8; negativo do TMU parado |
| CCN e cabo (`memory/sys_logical`) | `c182541` | 16/16 com as 179; 1 036 acessos e 0 fora do contrato por caso experimental; ruído 8/8 |
| ponte de interrupções (`cpu/irq_bridge`) | `9ecdeb7` | 16/16 com as 179; `BS_INTC_TRACE` inteiro igual ao do build anterior nos 16; entregas = as do N8.0; 0 fora do contrato; ruído 8/8 |

Revisões Codex com os achados aceitos e corrigidos com teste:
- `codex-20261025-224651` (TMU): o espelho da área 7 e os testes de underflow, rebase, entrega e reset;
- `codex-20261026-000604` (CCN e cabo): o espelho do CCR no modelo, os tamanhos dos arrays e o reset e os contadores;
- `codex-20261026-011016` (ponte), médio: uma entrega ao modelo dentro de um tratador deixaria a IRQ seguinte com o
  INTEVT antigo. Corrigido, com teste.

Unitários 314/314; ctest Debug 18/18 com e sem disco.

**Encerramento:** no tempo lógico (padrão da janela), o caminho de execução não depende dos ciclos (prova do ruído) e
não alcança os modelos do TMU, INTC, CCN não gráfico nem da porta.

**Ficam por decisão do mantenedor (D-N8.1-A/B) até o N8.2:**
- o estável por ciclos (`--tempo nativo`, padrão do headless, gravações antigas);
- os modelos `sh4_intc`/`sh4_tmu`/`sh4_ccn`/`sh4_port`/`time_legacy`, que servem o estável e são o recuo fora do
  contrato;
- as macros do gerador.

**Pendências:**
- B11/E6, a D7 com o TMU parado (diagnóstico do estável), vai para o N8.2 com o alvo de validação;
- a instrução de leitura do TMU no código do guest sai com o gerador.

Revisão Codex final do N8.1 inteiro (`codex-20261026-093724`, base `45826af`): 2 achados de documentação, aceitos e
corrigidos. O plano dizia que o `--tempo nativo` ficaria sem ciclos "após o N8.1" (é após o N8.2, D-N8.1-A); a
ARCHITECTURE dizia que PVR/Maple/GD-ROM deixam de agendar tempo (agendam no relógio lógico). Nenhum achado no código.

**Validação final** (exe final, 2026-10-26; Preview e Builder refeitos porque o runtime mudou):

| Teste | Resultado |
|---|---|
| `test_n8_nativo` | 26/26 |
| `test_n2b2` | 17/17 (N12: experimental 15,33 ms × estável 18,10 ms por campo) |
| `test_assistido` | 28/28 |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 |
| `test_preview` | 9/9 (Preview `9ecdeb7`) |
| H9 na janela (tempo lógico, Vulkan) | 0 faltas, 0 erros, 0 perdas em 8 978 blocos |

**N8.1 concluído.** O N8.2 só com autorização explícita.

### N8.2: separação do produto e da validação (2026-10-26)

Autorizado pelo mantenedor em 2026-10-26, só o N8.2. Detalhes e inventário em `docs/N8-2-SEPARACAO.md`.

Decisões do mantenedor:
- o tempo estável (`--tempo nativo`) e a instrumentação das referências ficam no produto e na validação
  (compartilhados): a CLI e o headless não mudam, e as 16 referências conferem o próprio exe do produto;
- só as mutações e sondas exclusivas de teste saem do produto.

**Separação:**
- `bs_runtime` montado de bibliotecas de objetos por papel: `aot`, `nativo`, `simulacao`, `estavel`, `diagnostico` e
  `validacao` (ou `validacao_stubs`), com os mesmos objetos no produto;
- opção `BS_VALIDACAO` e preset `msvc-validacao`. O produto (`msvc-release` e o Builder) tem stubs que recusam
  `BS_CICLOS_RUIDO`, `BS_HW_PROBE` e `BS_CRI_DIAG` (saída 2, nunca no vazio); o Debug e as pastas `mods`/`watch` são
  de validação;
- `bs_disc_data` (produto) separado do `bs_disc` (leitor de CDI e extrator, ferramentas).

**Catraca `n8_produto_papeis`** (ctest):
- papéis;
- o manifesto completo contra o mapa do linker (38 objetos no produto, 40 na validação; só `snapshot` fora, sem
  chamador);
- o mapa do mesmo link;
- o comportamento.

Os negativos (configuração cruzada, objeto ausente, mapa velho) reprovam. A `test_preview` confere a recusa das três
variáveis na Preview.

**Prova de que nada mudou:**
- produto: 16/16 com as 179 exceções e nenhuma nova;
- o `BS_INTC_TRACE` byte a byte igual ao build final do N8.1 nos 16 casos;
- ruído de ciclos no build de validação: experimental 8/8, estável 0/8 (controle).

| Teste | Resultado |
|---|---|
| unitários | 314/314 nas duas configurações |
| `test_n8_nativo` | 26/26 |
| `test_n2b2` | 17/17 (N12: experimental 15,43 ms × estável 17,96 ms) |
| `test_assistido` | 28/28 |

Revisão Codex `codex-20261026-115837`: 1 achado baixo aceito (a catraca amostrava objetos e não ligava o mapa ao exe:
agora o manifesto completo e a data do mapa).

**Validação final** (2026-10-26; Builder e Preview refeitos):

| Teste | Resultado |
|---|---|
| ctest Debug (validação) | 19/19, com e sem disco (inclui `n8_produto_papeis`) |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 |
| `test_preview` | 12/12 (as 3 recusas novas) |
| H9 na janela | 0 faltas, 0 erros, 0 perdas |

Tamanho: o exe do produto tem 19 248 128 bytes; o de validação, como a Preview anterior, 19 274 240 (26 KB das
mutações e sondas). O pacote do Builder tem 40,9 MB.

Hashes: produto (`msvc-release`) `3D17F679…`; validação `76D1CD9A…`; Preview `CC370D56…` (`3f203f5` + N8.2); a Preview
anterior (`9ecdeb7`) fica em `dist\BlueStinger-Preview.anterior` para o rollback.

**N8.2 concluído.** O N8.3 só com autorização explícita.

### Roadmap N0–N10 (decisão do mantenedor, 2026-10-22)

A etapa **N9 — Remasterização** entrou antes dos mods. O antigo escopo de mods e a elevação dos sistemas do jogo
viraram a **N10**. N0–N8 mantêm os objetivos e a ordem. Ordem de execução:
1. N7 permanece **funcional, 95%**, com AMD e fidelidade de fog/volumes abertas; isso não bloqueia N8.0–N8.6, mas
   fog/volumes bloqueiam o corte gráfico N8.7 e o fallback software não pode ser retirado;
2. retorno ao N2 — **concluído funcionalmente em 2026-10-24**;
3. N8 — iniciado em 2026-10-24 (D-N8-1 a D-N8-4); N8.0 concluído em 2026-10-25; N8.1 (N2c) e N8.2 concluídos em 2026-10-26; o N8.3 só com autorização explícita;
4. N9;
5. N10.

Plano: `docs/PLANO-PORT-NATIVO.md`, seções 4 e 4.2.

| Etapa | Situação | % |
|---|---|---|
| N0 Fundação | concluída | 100 |
| N1 Arquivos | concluída | 100 |
| N2 Tempo e eventos | **concluída funcionalmente** (2026-10-24): U1–U8 comprovados; experimental padrão na janela, estável no headless e em gravações antigas; `--tempo nativo` é o recuo. N2c abre o N8 | 100 |
| N3 Controles | concluída | 100 |
| N4 Saves | concluída (pendência formal: SAVE num save point real) | 100 |
| N5 Filmes | concluída | 100 |
| N6 Áudio | **concluída** (2026-10-23): H9 aprovado 3/3 com o Vulkan padrão (0 faltas); legado como fallback até o N8 | 100 |
| N7 Vulkan | **funcional, em fechamento técnico**: Vulkan padrão da janela com recuo seguro para o software; N7a, N7b e N7c validadas. Bloqueios: o DEVICE_LOST intermitente na AMD foi reduzido a um `GPU fault`/`Invalid Read`, mas o shader ou recurso de origem ainda não foi identificado; fog por tabela e volumes têm uso real localizado no render 1127, porém continuam sem implementação fiel e sem quadro pareado contra o Flycast. O fallback é mitigação. SSAO, 16:9 real e MSAA permanecem no N9 (`docs/N7-PLANO.md` seção 25) | 95 |
| N8 Sem legado | **iniciada** em 2026-10-24 (D-N8-1 a D-N8-4). N8.0 **concluído** em 2026-10-25 (regressão nível 3, correções, 16 referências validadas e reproduzidas, fixação do áudio nas suítes históricas); N8.1 **concluído** em 2026-10-26 (cadência B, TMU, CCN, cabo e ponte de interrupções no tempo lógico); N8.2 **concluído** em 2026-10-26 (produto × validação: papéis, `BS_VALIDACAO`, catraca do mapa); N8.3–N8.8 não iniciadas | 0 |
| N9 Remasterização | **não iniciada**; planejamento oficial aprovado em 2026-10-23 (`docs/N9-PLANO.md`): duas edições, Original e Remastered, do mesmo núcleo; começa depois do N8 | 0 |
| N10 Mods e elevação | não iniciada (extensões preparadas: mídia do N5/N6, texturas HD por conteúdo e shaders validados do N7) | 0 |

Progresso geral: ~72 % (onze etapas de peso igual).

### N7c: câmera livre e segundo analógico (2026-10-22; worktree n7-vulkan)

Aprovada pelo mantenedor (2026-10-22): interceptação da montagem da câmera filtrada pelo ponto de chamada, sem
escrever em `0x8C24CD50`. Desenho, semântica e evidências: `docs/N7-PLANO.md` seções 23.1 e 23.2.

**O que o jogador tem:**
- analógico direito gira a vista em volta do Eliot, e R3 recentraliza;
- três modos em Esc/Back > Controles > **Câmera** (ou `--camera`):
  - **Original:** a câmera do jogo, nada instalado (padrão);
  - **Híbrida:** volta sozinha depois do tempo escolhido;
  - **Livre:** fica até nova entrada ou R3;
- opções: sensibilidades, zona morta, inverter Y, suavização, retorno automático, R3 e limites de pitch;
- as opções ficam na seção `[camera]` do `controles.toml`; gravar troca só essa seção.

O analógico esquerdo e o movimento não mudam. Cinemáticas, diálogos e câmeras de evento (`CD68` ≠ 0) e cortes de
câmera zeram o deslocamento.

**Como:**
- as seis chamadas da montagem (`8C052240`) são diretas no código gerado: `sh4_override` não as alcança (correção da
  seção 23). Por isso as seis funções do SDK entraram em `[native] slots`; o gerado só troca a macro dessas chamadas,
  conferido arquivo a arquivo;
- `transitional/free_camera` (rig puro e wrappers) altera os argumentos só com o PR da montagem do gameplay.

**Validação** (método e números em `docs/N7-PLANO.md` 23.2):

| Teste | Resultado |
|---|---|
| Original, rota de combate | 9000/9000 quadros idênticos à referência e o mesmo trabalho do guest |
| Híbrida com o analógico parado | 9000/9000 idênticos |
| Livre e Híbrida com deslocamento (fixo, por roteiro e varrendo na exploração) | estado por passo idêntico ao Original (`BS_STATE_PASSO_SEM_CAMERA`): 4321, 7821 e 2332 passos, este último carregando um save, com as pastas de saves iguais |
| Vulkan × software com o mesmo deslocamento | 0 pixels diferentes |
| Janela com o controle virtual do SDL | o analógico real gira, o limite de pitch segura e o R3 recentraliza |
| Menu | troca de modo ao vivo; a seção gravada sem mudar as outras linhas; lida no início seguinte |
| Unitários | 298/298, com os seis slots exercitados por sentinelas: PR estrangeiro, fora de ordem, `CD68`, RAM da câmera do jogo intacta e Original devolvendo os slots |

**Revisão Codex** (`reviews/codex-20261022-151544.md`): três achados, todos aceitos e corrigidos.
- **[MÉDIA]** Faltava um teste dos wrappers com `Sh4Context`. Criado `test_camera_slots.cpp`.
- **[BAIXA]** O espectador anunciava o XInput de reserva, que a câmera não lê. A mensagem foi corrigida.
- **[BAIXA]** O Original lia e abria os diagnósticos (roteiro, log). Agora ignora; `BS_CAMERA_FIXA` foi para
  `Options`.

Pontos que o Codex conferiu sem problema:
- ordem e PRs das seis chamadas;
- argumentos;
- `~(yaw + Δ)`;
- `CD68` em 16 bits;
- nenhuma escrita em `CD50`;
- outros chamadores intactos;
- Original sem instalar nada;
- pedidos do menu com mutex e aplicados na thread do jogo;
- a câmera não chega ao pad do Dreamcast;
- gravação só da seção `[camera]`.

**Limitações:**
- objetos perto da câmera original e sprites orientados por ela seguem a câmera do jogo;
- sem colisão da câmera orbitada;
- `CD68` comprovado na rota de combate (tratamento conservador fora dela);
- movimento relativo à câmera vista fica para depois (opção separada, após validar mira, combate e animações).

### N7c: pós-processamento e shaders de mod (2026-10-22; worktree n7-vulkan)

**O que o jogador tem** (Esc/Back > Gráficos > **Efeitos de imagem**; só com a apresentação Vulkan):
- **Correção de cor:** liga e desliga, com brilho, contraste, gamma e saturação. Vale na hora (no quadro seguinte,
  inclusive com o jogo pausado no menu).
- **Shader de mod:** escolhe um pacote de `mods\shaders`, com os parâmetros do manifesto no menu.
- Tonemapping, nitidez, bloom, SSAO e CRT aparecem "em breve".
- **Os efeitos tocam só a imagem do jogo:** menu e legendas são compostos depois, nítidos.
- **Presets:**
  - Original Dreamcast: nenhum passe; o caminho é exatamente o de antes;
  - Equilibrado e Alta Qualidade (os "aprimorados"): ligam a correção de cor;
  - Personalizado: guarda tudo em `graficos.toml` (`[efeitos] cor/brilho/contraste`, `[imagem] shader`,
    `[shader_parametros]`).
- Formato dos pacotes, interface e validação: `docs/MODS-SHADERS.md`. Exemplo em `mods-exemplos/shaders/cor-quente`
  (fonte e manifesto; o SPIR-V é gerado, `tools/dev/n7_shader_exemplo.py` o instala sem o SDK).

**Arquitetura** (`docs/N7-PLANO.md`, seção 21):
- `native/render/post_shader`: o validador próprio de SPIR-V e o manifesto (portáveis, testados sem GPU).
- `native/render/vulkan/vk_post.cpp`: a cadeia, com passes de tela cheia alternando duas imagens RGBA8 na
  resolução da fonte. O passe interno de correção de cor usa a mesma interface dos externos.
- `vk_present.cpp` grava a cadeia antes do passe da swapchain.
- `app/window_present` carrega os shaders por dispositivo (de novo depois de uma perda).
- O menu desativa e grava um shader que o driver recusar ou que estiver ativo numa perda do dispositivo.

**Testes:**
- `bs_render_tests` 29 de 29:
  - `post_shader_validator`: o interno e o exemplo passam; recusados com o motivo:
    - magia errada, SPIR-V truncado, instrução com operandos de menos;
    - estágio de vértice;
    - buffer de armazenamento, push constants de 144 bytes, laço, `sampler3D`;
  - `post_shader_manifest`: pacote válido; caminho com `..`, link simbólico para fora do pacote, parâmetro com faixa
    invertida, sem o `.spv`, SPIR-V proibido;
  - `post_chain_gpu`: sem passes a imagem é idêntica; correção neutra com diferença máxima 0; saturação 0 dá cinza;
    brilho +0,2 sobe a média (117,5 para 166,2); o exemplo externo esquenta (vermelho 126 para 137,5, azul 110 para
    99); dois passes encadeados; 0 objetos Vulkan vivos no fim.
- `bs_unit_tests` 289 de 289 (`graphics_post_effects`, `settings_menu_effects_page`).
- **Janela com o jogo real e o controle virtual** (correção de cor, brilho +0,20 e o shader `cor-quente` pelo menu),
  média RGB da área do jogo nas capturas da imagem apresentada:

| Execução | VBlank 300 (antes do menu) | VBlank 600 | VBlank 900 | Quadros com pós-processamento |
|---|---|---|---|---|
| sem efeitos | 5,6 / 7,2 / 11,4 | 6,5 / 6,5 / 6,5 | 1,4 / 1,1 / 2,2 | 0 |
| efeitos pelo menu | 5,6 / 7,2 / 11,4 (igual) | **53,5 / 51,2 / 48,6** | 49,6 / 46,9 / 45,6 | 1591 |
| reinício (gravado) | 52,9 / 52,1 / 53,4 | 53,5 / 51,2 / 48,6 (igual) | 49,6 / 46,9 / 45,6 (igual) | 2382 |

- **Pacotes inválidos** instalados (`teste-buffer`, `teste-corrompido`, `teste-manifesto`):
  - os três foram recusados com o motivo no log;
  - o menu mostra "Shaders inválidos: 3";
  - o jogo segue normal.
- Com 1080p interno, as duas coisas juntas: 2340 quadros direto da GPU, 1591 com pós-processamento.
- Gameplay real na janela (`n7_janela.py`, efeitos desligados) continua APROVADO.

**Revisão Codex** (`reviews/codex-20261022-093953.md`), 7 achados, aceitos:

| Achado | Correção |
|---|---|
| [ALTA] leituras fora do SPIR-V com instruções curtas | mínimo de operandos por opcode e strings terminadas dentro da instrução (teste) |
| [ALTA] shader com execução ilimitada passava | laços recusados (teste); uma perda do dispositivo com o shader ativo o desativa; laços com orçamento no N10 |
| [ALTA] semáforo do slot podia ser sinalizado de novo sem espera | só com duas filas, e um semáforo novo a cada escrita do slot |
| [MÉDIA] `sampler2D` e o limite de 80 bytes não eram conferidos de fato | `OpTypeImage` 2D simples exigido; push constants só com layout explícito (teste com `sampler3D`) |
| [MÉDIA] escape por link simbólico ou junção | caminhos canônicos dentro do pacote (teste com link real) |
| [MÉDIA] falha ao recarregar depois de uma perda não desativava | `take_post_error`: o menu desativa e grava |
| [BAIXA] o teste da GPU não cobre a sincronização da apresentação | coberta pelas janelas com gameplay real e pelo J1 com as camadas de validação (2026-10-22, 0 mensagens) |

**Pendente nesta frente:**
- tonemapping, nitidez e bloom (internos, um por vez);
- SSAO, só se a profundidade servir;
- iluminação e sombras: interface reservada, sem dados inventados;
- várias passadas, LUTs e ferramentas: N10.

### N7c: apresentação direta pelo Vulkan, caminho B (2026-10-22; worktree n7-vulkan)

**O que o jogador tem:**
- A janela é desenhada pelo **mesmo dispositivo Vulkan** que renderiza o jogo, numa swapchain própria. É o padrão
  quando há Vulkan; `--apresentacao sdl` ou `BS_APRESENTACAO=sdl` usam o SDL_Renderer, que também é o recuo
  automático se a apresentação Vulkan falhar.
- **Resolução interna** original, 720p, 1080p, 1440p e 4K pelo menu (só com o renderizador Vulkan e a apresentação
  direta), com confirmação e reversão em 15 s, limitada pelo que a GPU comporta: lado máximo das imagens e ~1/4 da
  memória local.
- **A imagem maior não passa pela CPU:**
  - cópia na GPU para um anel de 4 slots;
  - redução na GPU (`downsample.comp`, as mesmas contas inteiras da CPU) e só ela volta (1,2 MB, para a VRAM
    emulada continuar coerente);
  - a apresentação desenha o slot do render exibido na área 4:3 e as camadas nativas (legendas, menu) por cima, na
    resolução da janela, enviadas só quando mudam.
- O renderizador em software continua disponível: a imagem dele (e a dos filmes) vai como textura da CPU, enviada a
  cada quadro novo.

**Arquitetura** (`docs/N7-PLANO.md`, seção 20):
- `vulkan_shared`: um dispositivo por processo. O renderizador (thread do jogo) e a apresentação (thread da janela) o
  compartilham, cada um na sua fila da mesma família.
  - Com uma fila só, há uma trava comum. Medido: com uma fila só, o `vkQueuePresentKHR` em V-Sync segurava o jogo
    (14,3 ms por campo); com duas filas, 5,7 ms.
- `vk_internal.h`: tabela de funções, auxiliares de memória e as filas.
- `vk_present.cpp`: a apresentação.
- `app/window_present`: a interface da janela (Vulkan ou SDL) que o laço e o menu usam.
- **Sincronização dos slots:**
  - layout `GENERAL` fixo;
  - a escrita sinaliza um semáforo que a primeira apresentação do slot espera;
  - a fixação (`pin`) só cai depois da cerca da apresentação;
  - o renderizador só reaproveita slots sem fixação.

**Testes:**
- `bs_render_tests` 26 de 26 (a redução da GPU é **igual** à da CPU em todas as cenas, escalas 1,5 e 2,25);
  `bs_unit_tests` 287 de 287.
- Rota `passo_combate`, RTX 5080:

| Medida | Original | 1080p A (CPU) | 1080p B | 1440p B | 4K B |
|---|---|---|---|---|---|
| Quadros na VRAM | 9000/9000 idênticos à referência | — | **idênticos aos do A** (9000/9000) | — | — |
| Imagem maior no VBlank exibido | — | 8967/8967 | 8967/8967 | 8967/8967 | 8967/8967 |
| GPU por quadro (média) | 1,41 ms | 2,83 ms | 1,95 ms | 2,13 ms | 2,91 ms |
| Leitura de volta na CPU | (1,2 MB) | 3,5 ms (6,2 MB) | 0,18 ms (1,2 MB) | 0,19 ms | 0,20 ms |
| Trabalho por campo (média) | 7,06 ms | 9,25 ms | 7,44 ms | 7,58 ms | 7,94 ms |
| Qualidade (`n7_resolucao.py`, 18 quadros) | — | aprovado | aprovado (capturas iguais às do A) | aprovado | aprovado |

- **Janela com gameplay real** (`tools/dev/n7_janela.py`, reprodução da gravação `sessao-20261021-145006` no modo
  espectador, 18 capturas da imagem apresentada nas primeiras falas, 1080p):

| Saída | Apresentação | Resultado |
|---|---|---|
| janela 1280×960 (4:3) | Vulkan | APROVADO: legenda em 11 de 11 capturas com fala, nenhuma nas 7 sem fala; área 4:3 e imagem conferidas |
| sem bordas 2560×1080 (21:9) | Vulkan | APROVADO (faixas pretas laterais, jogo 4:3 centrado) |
| janela 1600×900 (16:9) | Vulkan | APROVADO |
| janela 1280×960 | SDL (recuo) | APROVADO |

- **Menu na janela com a apresentação Vulkan:**
  - sem bordas **sem** confirmar volta sozinho e não é gravado; confirmado, é gravado (capturas 2560×1080);
  - swapchain recriada a cada troca;
  - menu a 4K interno: 1364 de 1421 quadros apresentados direto da GPU; trabalho da CPU da apresentação 0,26 ms por
    quadro (esperas e V-Sync à parte).
- Legendas no headless (`n7_legendas.py`, capturas agora exatas por VBlank): software e Vulkan APROVADOS (866 quadros
  com fala, 339 sem).

**Revisão Codex** (`reviews/codex-20261022-090528.md`), 8 achados, todos aceitos:

| Achado | Correção |
|---|---|
| [ALTA] slots entre duas filas sem dependência Vulkan | semáforo por escrita do slot, esperado no estágio do fragmento pela primeira apresentação; a dependência inversa vem da cerca da apresentação antes de soltar a fixação |
| [ALTA] `read_slot` trocava o layout com a apresentação amostrando | slots sempre em `GENERAL`: cópia e amostragem sem transições |
| [MÉDIA] falha depois da aquisição deixava semáforo e cerca inutilizáveis | `resync`: swapchain, semáforo e cerca refeitos; cerca zerada logo antes do envio |
| [MÉDIA] captura sem `TRANSFER_SRC` na swapchain | `canCapture_`: sem o uso, a captura fica indisponível |
| [MÉDIA] versões de menu e legenda no mesmo índice de camada | índices fixos (0 legendas, 1 menu) |
| [MÉDIA] perda do dispositivo vista pela apresentação não invalidava o compartilhado | `vulkan_shared_lost` antes de refazer |
| [MÉDIA] swapchain parcial marcada como completa | construída em temporários, publicada só completa |
| [BAIXA] testes não exercitam o caminho concorrente | as janelas com gameplay real o exercitam; J1 com as camadas de validação (2026-10-22): 0 mensagens |

**Limites conhecidos:**
- 16:9 de verdade (projeção, câmera, HUD) não foi feito: em telas largas o jogo fica 4:3 com faixas.
- A perda do dispositivo vista pela apresentação foi tratada pelo código, mas não provocada em teste.
- Na GPU AMD integrada a apresentação direta não foi testada (a pendência do N7b continua aberta).

### N7c: resolução interna, caminho A (validação mínima do 1080p) (2026-10-22; worktree n7-vulkan)

**Decisão do mantenedor (2026-10-22):** seguir A → B. A é uma versão mínima para validar a renderização interna em
1080p com a infraestrutura existente (não é uma arquitetura permanente); B, a apresentação direta pelo Vulkan
(swapchain), é o caminho definitivo.

**O que mudou** (`docs/N7-PLANO.md`, seção 20):
- **Escala no modo fiel.** O Vulkan desenha a geometria num alvo `round(640 × e) × round(480 × e)`, com
  `e = altura interna / 480`.
  - Cada pixel do alvo roda as mesmas contas do software (cobertura, 1/w, atributos), num ponto de amostra entre
    os pixels do jogo: `(pixel + 0,5) × 1/e`.
  - Na escala 1, a multiplicação por 1,0 é exata e o modo fiel continua bit a bit.
  - `frame_prep` leva o retângulo envolvente e o recorte "dentro" para os pixels do alvo; o recorte "fora" usa o
    pixel do jogo que contém o centro.
- **Framebuffer coerente.** A imagem maior, reduzida pela média (`downsample`), vai para a VRAM emulada. O resto do
  caminho (capturas, hash dos quadros, recuo) continua igual.
- **Imagem exibida.** O quadro exibido é localizado entre os 3 últimos renders pelo conteúdo, tolerando a
  quantização do framebuffer. O vídeo entrelaçado mostra 640×476 de um render de 640×480, então o recorte vertical
  é procurado. A janela recebe a imagem maior nas linhas exibidas.
- **No caminho A** a imagem maior volta inteira para a CPU (o custo que o B elimina).
- **Interface:**
  - Menu: "Resolução interna" (original, 720p, 1080p; só com Vulkan; 1440p e 4K bloqueados até a apresentação
    direta estabilizar) e "Escala de renderização", ambos com confirmação e reversão em 15 s.
  - Linha de comando: `--resolucao-interna <altura>`, só nesta execução.
  - As capturas do headless passaram a ser feitas na thread do jogo, nos VBlanks exatos (`--dump-every`), com
    `vNNNNNN_hd.bmp` na resolução interna.

**Testes:**
- `bs_render_tests` 26 de 26, com dois novos:
  - `render_vulkan_internal_resolution`: as provas das cenas fiéis nas escalas 1,5 e 2,25; a borda de um
    triângulo desenhada na escala 3 tem degraus fora da grade da imagem original (80 de 120 linhas); a reduzida
    volta perto da original;
  - `render_downsample_box`.
- `bs_unit_tests` 287 de 287.
- Rota `passo_combate` (9000 VBlanks, `--video legado`, `BS_RELOGIO_FIXO`), RTX 5080:

| Medida | Original (`--resolucao-interna 480`) | 1080p (1440×1080) |
|---|---|---|
| Quadros contra a referência do software | **9000 de 9000 idênticos** | (a VRAM recebe a reduzida, diferente por definição) |
| VBlanks exibidos com a imagem maior | — | 8967 de 8967 |
| GPU por quadro (média) | 1,44 ms | 2,83 ms |
| Leitura de volta + redução na CPU | — | 3,5 ms por render (6,2 MB por quadro) |
| Trabalho por campo (média) | 7,1 ms | 9,3 ms |

- **Qualidade** (`tools/dev/n7_resolucao.py`, 18 quadros a cada 500 VBlanks, mesmos VBlanks nas duas execuções):
  - a reduzida contra a original fica entre 29,6 e 44,2 dB (é o mesmo quadro; só a cobertura parcial das bordas
    muda);
  - o detalhe acima da resolução original é 1,40 a 2,08 vezes o da original ampliada (ampliar daria 1,0);
  - a imagem maior contra a original ampliada fica entre 21 e 41 dB (é outra imagem);
  - recorte visual: placas ("WELCOME to KIMRA Corp.") e bordas legíveis a 1080p, ilegíveis na original.
- **Janela:** com o menu aberto sobre a imagem 1440×1080 em 1280×960, os 668 VBlanks exibidos usaram a imagem maior.

**Limites do A** (resolvidos no B): a imagem maior passa pela CPU (6,2 MB e 3,5 ms por render a 1080p; a 4K seriam
~33 MB) e a apresentação é a do SDL.

### N7c: menu interno de configurações (2026-10-22; worktree n7-vulkan)

**O que o jogador tem** (Esc no teclado ou Back no controle, em qualquer tela: título, menu principal, gameplay):
- O menu abre por cima do jogo, que fica **pausado entre VBlanks** (`GameHost::setPaused`: a thread do jogo espera
  numa variável de condição no início do VBlank, sem tocar a lógica do Dreamcast; áudio e a fila da saída XAudio2
  pausados junto; o relógio de tempo real recomeça na volta, sem correr atrás).
- **Navegação:** controle (direcional, analógico com repetição, A confirma, B volta, LB/RB mudam o valor), teclado
  (setas, Enter, Esc/Backspace) e mouse (passar seleciona, clique nas setas muda, clique confirma, botão direito volta,
  roda rola). Soltar teclas e botões continua indo para o jogo (nada fica "preso" ao fechar).
- **Gráficos:** renderizador software ou Vulkan (troca na hora, entre quadros, na thread do jogo; só aparece ativo se
  o Vulkan foi detectado), modo de tela (janela, sem bordas, tela cheia), tamanho da janela (640×480 a 1920×1440),
  V-Sync, predefinição. Resolução interna, 16:9, filtragem, AA, shaders, sombras, iluminação e pós-processamento
  aparecem como **"em breve"**.
- **Áudio:** volumes geral, música, efeitos e vozes aplicados de verdade no mixer do N6 (`sound::set_bus_volumes`).
  Com `--audio legado` os volumes não têm efeito (o legado não tem barramentos).
- **Legendas:** ligar/desligar, idioma (dos pacotes em `mods`), tamanho, contorno e fundo; valem na hora para as
  falas e para os filmes.
- **Controles:** mostra o perfil e o arquivo `controles.toml` do N3; remapear pelo menu aparece como "em breve".
- **Restaurar padrões** (com confirmação).

**Persistência e segurança:**
- `graficos.toml`, `audio.toml` e `legendas.toml` na pasta do usuário, escrita atômica (temporário + troca); valores
  inválidos voltam ao padrão com aviso.
- Mudança de tamanho ou modo de tela: aplicada na hora com a pergunta "Manter?" e **volta sozinha em 15 s**; só vai
  para o arquivo depois de aceita. Se o SDL recusa o modo, a configuração anterior volta; se o modo salvo falha na
  inicialização, o jogo abre em janela no tamanho padrão (nunca fica sem imagem).
- Precedência: linha de comando > arquivos > `instalacao.toml` > padrões. `--render` e `--legendas` valem só na
  execução e não sobrescrevem o arquivo, a não ser que o jogador mude esse item no menu.
- A imagem do jogo não muda: o menu e as legendas são uma camada nativa por cima (Direct2D/DirectWrite, na
  resolução da saída), igual no software e no Vulkan. A área do jogo é o maior retângulo 4:3 centrado na saída,
  calculada a cada quadro (`gameArea`), com as camadas no mesmo espaço de pixels: nítidas em qualquer tamanho de
  janela e em telas 16:9 ou 21:9 (faixas pretas).

**Testes:**
- Unitários: `bs_unit_tests` 287 de 287 (novos: `menu_overlay_draws_panel_and_hit_areas`, ordem das linhas, setas e
  pergunta bloqueando cliques; `game_host_pause_holds_vblanks_and_applies_requests`, nenhum VBlank durante a pausa,
  renderizador trocado com o jogo pausado sem despausar, parada pedida durante a pausa encerra sem erro;
  `settings_menu_*` e `graphics_*` atualizados). `bs_render_tests` 24 de 24.
- Janela real com o jogo e controle virtual do SDL (`BS_CONTROLE_VIRTUAL`, capturas da imagem apresentada com
  `BS_MENU_CAPTURA`), RTX 5080, monitor 2560×1080:

| Caso | Resultado |
|---|---|
| Abrir no título, legendas desligadas, volume geral 90, fechar | pausado do VBlank 525 ao 526 (~7 s), `legendas.toml` e `audio.toml` gravados, "[legenda] idioma: desligadas" aplicado |
| Reiniciar | abre com as legendas desligadas (persistência) |
| Vulkan pelo menu + sem bordas **sem** confirmar | troca para o Vulkan na hora (dispositivo criado no quadro seguinte); a tela volta sozinha; `graficos.toml` com `renderizador = "vulkan"` e `modo = "janela"` |
| Vulkan + sem bordas confirmado | `modo = "sem_bordas"` gravado; captura 2560×1080 com o jogo 4:3 centrado e o menu nítido |
| Reiniciar com `--render software` e mudar só as legendas | `graficos.toml` continua `vulkan` (a opção da linha de comando não contamina o arquivo) |
| Rota de combate headless depois das mudanças do host | quadros idênticos à referência (237 s) |

**Revisão Codex** (`reviews/codex-20261022-015523.md`), 5 achados, todos aceitos e corrigidos:

| Achado | Correção |
|---|---|
| [ALTA] modo de tela gravado antes da confirmação; retornos do SDL ignorados | grava só a tela aceita (`confirmed_`); `applyWindow` confere `SDL_SetWindowFullscreen/Mode/Size` e `SDL_SyncWindow`; falha volta a anterior (`cancel_display`); falha na inicialização abre em janela |
| [MÉDIA] pedidos do menu não aplicados com o jogo pausado | a espera da pausa acorda com os pedidos e os aplica na thread do jogo, sem despausar (teste unitário) |
| [MÉDIA] a fila da saída de áudio (até 200 ms) continuava tocando na pausa | `sound::pause` pausa também a saída XAudio2 |
| [MÉDIA] opções da linha de comando gravadas nos arquivos | preferências efetivas e persistidas separadas (teste em janela acima) |
| [BAIXA] faltavam testes de integração | teste do `GameHost` (pausa, pedidos, parada) e os casos em janela acima |

**Limites conhecidos:** não há uma opção "Configurações" desenhada no menu do título do jogo (exigiria reconhecer a
tela; a tecla funciona em qualquer tela). Só a predefinição "Original Dreamcast" é real; as outras dependem das
resoluções e efeitos. Volumes sem efeito com o áudio legado.

### N7c: legendas no gameplay, modelos das configurações e do menu (2026-10-22; worktree n7-vulkan)

**Legendas das falas na imagem** (`docs/N7-PLANO.md`, seção 18):
- **Origem do texto:** o `SubtitleService` do N6 (sem nova lógica de eventos ou sincronização),
  publicado com o quadro do mesmo VBlank (`GameFrame::subtitle`).
- **Desenho:** `app/subtitle_overlay` usa o rasterizador DirectWrite do N5 numa camada transparente
  na resolução da janela, por cima do jogo. A imagem do jogo não muda: o hash dos quadros e as
  comparações de fidelidade não a veem. Funciona igual com software e Vulkan.
- **Capturas:** compõem a legenda (o que o jogador vê).

**Teste de aceitação em gameplay real:** `tools/dev/n7_legendas.py` com a gravação
`sessao-20261021-145006`:
- reproduz a gravação e captura as faixas das falas que o N6 anuncia;
- exige a legenda **na imagem** (quadro composto ≠ cru, só na faixa de baixo) durante cada fala e
  **nenhuma** fora delas.

| Renderizador | Quadros com fala, com a legenda na imagem | Quadros sem fala, limpos | Resultado |
|---|---|---|---|
| software | 866 | 338 | APROVADO |
| Vulkan | 860 | 341 | APROVADO |

As falas cobertas são as 17 linhas de `voz/e025a`, `voz/e025b` e `voz/e005b`, sobre música, com
troca de cena. Conferido também visualmente: uma captura do Vulkan mostra a legenda da fala embaixo,
legível.

**Modelos** (sem interface ainda; o menu funcional vem depois):
- `native/platform/graphics_settings`:
  - presets, faixas e persistência (`graficos.toml`);
  - `Features`: só o que o renderizador implementa aparece ativo;
  - `apply_kind`: imediato ou recriação;
  - confirmação com reversão em 15 s;
  - proporção automática e tamanho da janela.
- `native/platform/user_settings`: `audio.toml` (volumes) e `legendas.toml` (ligadas, idioma,
  tamanho, contorno ou fundo; inglês ligado por padrão).
- `app/settings_menu`: o modelo do menu interno.
  - Páginas: Gráficos, Áudio, Legendas, Controles.
  - Navegação por ações (controle, teclado e mouse).
  - Opções não implementadas aparecem como "em breve".
  - Confirmação e reversão automática; "Restaurar padrões".
  - O pedido do mapeamento do N3.

**Testes:** `bs_unit_tests` 285 de 285, entre eles:
- `subtitle_overlay_*` (2);
- `graphics_*` (5);
- `user_settings_*` (1);
- `settings_menu_*` (4).

**Pendente no N7c** (depois do menu, seção acima): resoluções modernas e efeitos (as `Features` acendem conforme
ficam prontas).

### N7b: backend Vulkan (2026-10-22; worktree n7-vulkan)

**O que foi feito** (detalhes em `docs/N7-PLANO.md`, seção 14):
- **Dispositivo:** `native/render/vulkan/vk_device` implementa o `IRenderDevice` (offscreen). O Vulkan
  é carregado em tempo de execução, sem SDK. Os cabeçalhos Khronos 1.3.282 ficam em
  `third_party/vulkan`, que entrou no kit do Builder.
- **Shaders:** em GLSL, com o SPIR-V versionado (`tools/dev/gera_spirv.ps1 [-Conferir]`).
- **Preparação do quadro:** na CPU (`native/render/frame_prep`).
- **Integração:**
  - `--render nativo|software` (ou `BS_RENDER`); o software continua padrão;
  - `BS_RENDER_IR=comparar` (com `BS_RENDER_IR_BMP` e `_DIFF`);
  - `BS_VULKAN_GPU=<nome>`;
  - recuperação da perda do dispositivo (até 3 por sessão), com recuo para o software.
- **Modo fiel** (resolução original): as contas do `Raster::draw` no shader:
  - candidatos pelo retângulo envolvente, como o laço do software;
  - cobertura com a regra de cima/esquerda;
  - 1/w e atributos com a divisão **inteira** corretamente arredondada e a tabela exata de `c/255`;
  - profundidade crua (`VK_EXT_depth_range_unrestricted`);
  - autosort por pixel com ordenação local de até 128 camadas e seleção acima disso;
  - variantes de shader com arredondamento ao par e subnormais preservados conforme a GPU.
- **Texturas:** cache pelos bytes da VRAM; o decodificador do legado entra quando falta conversão
  nativa (contado, nunca descartado); substituição HD por `TextureId` (contrato do N7a).

**Método.** Rota `passo_combate` (9000 VBlanks, `--video legado`, `BS_RELOGIO_FIXO=1700000000`);
`BS_FRAME_LOG` comparado com a linha de base do software.

| Medida | NVIDIA RTX 5080 | AMD Radeon integrada |
|---|---|---|
| Garantias do modo fiel | arredondamento ao par, profundidade crua; subnormais no padrão do dispositivo | arredondamento ao par, subnormais preservados, profundidade crua |
| `comparar` (todo render) | **4487/4487 idênticos** (0 pixels diferentes) | 4486/4487 idênticos; 1 recriação do dispositivo (ver a pendência) |
| `--render nativo` | quadros **idênticos ao software** nos 9000 VBlanks; 69 s | quadros idênticos; 102 s; 0 falhas (2 rotas) |
| Software (referência) | 224 s | — |
| GPU por quadro | média 1,48 ms, pior 12,7 ms | média 8,6 ms, pior 28 ms |
| Camadas translúcidas por pixel | até 78 | até 78 |

**Testes:**
- `bs_render_tests`: 24 de 24, nas duas GPUs.
- `bs_unit_tests`: 285 de 285 nas duas GPUs. Entre eles, a imagem inteira Vulkan × software em 12
  cenas, todas **idênticas**:
  - gouraud, fatias finas, sprite, recorte, textura bilinear com espelho;
  - `HALF_OFFSET` 1 e 3, 1/w negativo e o contraexemplo de -16777216;
  - autosort com 6, 70 e 140 camadas;
  - mais: falhas injetadas (textura; dispositivo transitório e permanente), substituição HD e
    reprodução de um render gravado (`BS_RENDER_IR_GUARDAR` / `BS_RENDER_IR_REPRODUZIR`).

**Desempenho.** A leitura de volta passou a usar memória `HOST_CACHED`; sem cache da CPU a rota
levava 514 s.

**Pendência de estabilidade (AMD integrada, modo `comparar`).** A perda do dispositivo
(`VK_ERROR_DEVICE_LOST`) é intermitente e pode ocorrer em renders diferentes conforme a sessão.
- O render 370 sai com a imagem anterior, mesmo com a cerca e a fila ociosa acusando sucesso.
- O mesmo render, gravado e reproduzido sozinho, dá na AMD a **mesma imagem da NVIDIA** (50
  repetições, sem falha).
- Não aparece no modo nativo (2 rotas completas), com atraso entre quadros nem com o software
  sequencial, e não há reset (TDR) no log do sistema.

A recuperação recria o dispositivo e a rota pode terminar com 1 quadro errado. Fica aberta até identificar a causa. As
Validation Layers rodaram em 2026-10-22: depois de corrigido um hazard de sincronização sem relação com a falha, não há
erros, e a perda continua (ver "N7c" → "AMD"). A hipótese de driver não está comprovada.

**Reinvestigação de 2026-10-24.** Em dez A1 sem camadas, nove passaram com 583/583 renders idênticos e uma perdeu o
dispositivo no render 470 (o render 469 ficou incorreto). Com `VK_LAYER_LUNARG_crash_diagnostic`, a primeira repetição
falhou na espera da cerca do render 370; `VK_EXT_device_fault` registrou `GPU fault` e `Invalid Read`. Como a camada não
instrumenta comandos dentro de render passes legados, o dump ainda não identifica o shader/recurso que fez a leitura.
A1 com Validation Layers completas + sincronização passou novamente com zero mensagens e zero recriações. Resultado:
falha real de acesso na GPU comprovada, causa de origem ainda aberta; fallback continua só mitigação.

Na rota `passo_combate` de 9000 VBlanks, a nova telemetria de cobertura localizou o primeiro uso real de fog por tabela
e de volumes modificadores no render 1127. Foram 2.758.908 desenhos com fog por tabela, nenhum com fog por vértice ou
`Table2`, 76.287 volumes e 2.109.830 triângulos de volume. O controle NVIDIA concluiu 4484 renders sem falha. Não há no
repositório uma captura Flycast pareada com esse render; fog e volumes continuam sem implementação fiel e sem aprovação
visual. A execução usou `--video legado --render nativo --headless 9000`, relógio fixo e o roteiro de combate; duas
repetições dirigidas até 3000 VBlanks confirmaram o primeiro uso no render 1127 e todas as contagens parciais. Os 4487
renders da tabela histórica acima são do executável anterior ao fechamento do N2. Evidências e limites completos:
`docs/N7-PLANO.md`, seção 25.6.

### N7a: captura do TA na IR de render (2026-10-21; worktree n7-vulkan)

**O que foi feito** (detalhes em `docs/N7-PLANO.md` seção 13):
- O decodificador do TA (`runtime/pvr/pvr_render.cpp`) ganhou um observador (`PvrCaptureSink`), sem
  um segundo parser.
- `transitional/render_capture` monta a `CommandList`: strips, sprites, estado completo, volumes
  modificadores fechados pela instrução do global, `FrameParams` e a tabela de texturas.
- O rasterizador passou a ler os registradores de quadro uma vez por render (`PvrFrame`).
- Diagnóstico: `BS_RENDER_IR=capturar|conferir`, `BS_RENDER_IR_CADA=N`. O padrão continua sem
  captura, com o renderizador em software.

**Contrato das texturas HD futuras** (`native/render/texture_replacement`): `TextureId` por
conteúdo decodificado, regras de compatibilidade e o ponto de extensão `ITextureReplacements`. Não
há carregamento nem upscale; as ferramentas e pacotes ficam no N10.

**Método.** Rota `passo_combate` (NEW GAME → combate), 9000 VBlanks, `--video legado`,
`BS_RELOGIO_FIXO=1700000000`, `BS_FRAME_LOG` comparado com a linha de base do executável do N6.

**Resultados:**

| Medida | Resultado |
|---|---|
| Quadros sem captura | idênticos à linha de base |
| Quadros com `conferir` | idênticos à linha de base |
| Renders conferidos | 4487 de 4487 |
| Desenhos / triângulos / sprites | 3.882.569 / 18.625.730 / 13.120 |
| IR inválida | 0 |
| Triângulos divergentes | 0 |
| Imagens divergentes (redesenho só a partir da IR) | 0 |
| `FrameParams` divergentes dos registradores usados | 0 |
| Texturas conferidas texel a texel | 186.083, 0 divergentes |
| Texturas sem conversão nativa | 140 |
| Chaves de textura distintas (TCW + tamanho) | 249 |
| Conteúdos de textura distintos | 121 |
| Volumes modificadores | 76.290 (2.109.836 triângulos), 1.173 sem último polígono |
| Parâmetros omitidos | 0 |

O jogo envia vértices com 1/w NaN ou negativo (1 render). O `validate()` passou a só conferir a
estrutura.

**Unitários:**
- `bs_unit_tests`: 269 de 269, incluindo 9 de `test_render_capture.cpp`;
- `bs_render_tests`: 18 de 18, incluindo 4 de `test_render_replacement.cpp`.

**Incidente da limpeza de 2026-10-21.** A remoção forçada das worktrees obsoletas apagou, por uma
junção, as fontes do SDL3 em `build/msvc-release/_deps/sdl3-src` do repositório principal. Elas
foram restauradas da tag fixada (`release-3.2.30`, clone raso do repositório oficial). Nenhum outro
dado foi afetado: dados da Preview, `game-data`, ISO, `generated`, legendas, gravações e saves
conferidos.

### N6-H: áudio nativo promovido a padrão, condicionalmente (2026-10-21; autorizado pelo mantenedor)

**Método:** verificação dirigida (H1–H8 reaproveitados): H5 refeito, `test_gameplay` com o padrão nativo e com
`BS_AUDIO=legado`, unitários normal e ASan.

- `kAudioNativeDefault = true`: efeitos, falas e música nativos por padrão; fallback `--audio legado`/`BS_AUDIO=legado` até
  o N8; precedência das opções individuais preservada; ajuda atualizada; legendas en-US por padrão inalteradas.
- H5 12/12 com o padrão novo; `test_gameplay` 4/4 nativo e 3/3 legado (interrompido pelo mantenedor no último caso);
  `bs_audio_tests` 80/80, `bs_video_tests` 45/45, `bs_localization_tests` 9/9, `bs_ui_tests` 25/25, normal e ASan.
- **Condicional:** H1–H8 aprovados; **H9 bloqueado pelo N2** (345 faltas de dados na última medição, sem mascarar).
- `n6_efeitos.ps1`/`n6_falas.ps1` fixam `--audio legado` para isolar o provedor testado.

### N6-H: pendências resolvidas (H4 e H8) e proposta de encerramento condicional (2026-10-21; sem commit, sem promoção)

**Método:** contador de clipping por fonte no mixer, H4 refeito (LOAD e heliporto), `test_saves` corrigido e refeito com o
áudio padrão e com `BS_AUDIO=nativo`, testes de áudio e vídeo normal e ASan, revisão Codex `reviews/codex-20261021-190156.md`.

- **H4 PASSOU** com o critério pelo modelo da AICA: 0 fontes acima de 16 bits nas duas rotas; o clipping de soma do
  heliporto (460) informado. Sem prova de equivalência acústica (registradores iguais ao original; lei de TL a do Flycast).
- **H8 PASSOU:** `test_saves` 25/0 com áudio nativo e 25/0 com o padrão, depois de corrigido o orçamento da rota de SAVE para
  os filmes nativos (falha anterior do N5, não do áudio); controles 30/0; E001 10/10.
- **H9 BLOQUEADO** pelo desempenho do N2, medições preservadas, limite inalterado.
- Testes: `bs_audio_tests` 80/80 e `bs_video_tests` 45/45, normal e ASan.
- Proposta de encerramento condicional: `docs/N6-PLANO.md` 20.4 (depende do N2 para o H9 e da avaliação auditiva).

### N6-H: validações independentes, caso a caso (2026-10-21; worktree n6-audio, sem commit, sem promoção)

**Método:** `tools/dev/n6_h.ps1` um caso por vez, memória e processos conferidos antes de cada um (≥ 18 GB livres; o
`test_pd_nativo` abriu 8 processos dentro do H8, depois limitado a 2 na bateria); casos com código inalterado reaproveitados;
revisão Codex `reviews/codex-20261021-155015.md` (7 achados, aceitos); M3 × Flycast reanalisada.

- **H1 PASSOU, H2 PASSOU, H3 PASSOU, H5 PASSOU** (reaproveitados ou refeitos depois das mudanças, ver o plano 20.2).
- **H6 PASSOU:** dublagens WAV, FLAC e OGG tocadas, sem falha de leitura, legenda ativa.
- **H7 PASSOU** com o critério completo nas 11 músicas.
- **H4 FALHOU** só na saturação do heliporto (460 amostras, durante o ambiente `e012aenv` e efeitos, sem música): estado,
  provedores e LOAD aprovados; a saturação é da classe caracterizada na G; critério mantido, revisão a decidir.
- **H8 INCOMPLETO:** controles 30/30 e E001 10/10 com áudio nativo; `test_saves` bloqueado por falha anterior do próprio
  teste (o roteiro não pula o filme NEWGAME com o vídeo nativo; igual com áudio legado), evidência alternativa nas gravações.
- **H9 BLOQUEADO** pelo desempenho do N2 (faltas 345 com a reserva; 0 erros, perdas, descartes); limite mantido.
- **M3 × Flycast:** 3 255/3 255 em amostra e tom a 100 ms; os 48 sem par estritos estão todos no 1º 1,4 s (porta em −113:
  o tempo do download, N2 11.1); 3 207/3 255 com todos os registradores fora disso; inversões = acordes com a deriva de tempo.
- Correções: teto de 200 ms na fila da saída; analisador da F (porta e r0 no ducking, pacotes como o jogo, cobertura,
  descartes); `.bsv` só com os limites do decodificador; a bateria chama os scripts filhos sem que o stderr a derrube;
  `bs-video-probe` no `build\n6`.

### N6-F encerrada, N6-G encerrada (saturação caracterizada) e a saída do jogo em tempo real (2026-10-21; worktree n6-audio, sem commit)

**Método:** duas gravações reais do mantenedor (`gravacoes.py`), análise automática `n6_f_sessao.py` (reprodução, nativo e
legado na mesma entrada, critérios), oráculo do Flycast (`--injetar-musica 2700:3` para a M3; registradores 0x2800 e
descritores do disco para a caracterização), revisão Codex, testes dirigidos normais e ASan, H9 na janela.

- **F concluída:** sessão 1 (Dogs: `e003` com a M2A) e sessão 2 (**M3** no passo 6548; `e025a/b`, `e005b`): reproduções
  determinísticas, ducking pedido pelo jogo e desfeito no estado 3, legendas en-US (16 e 17 linhas, ≤ 1 VBlank), estado igual
  ao legado até o 1º fim natural de fala, saves iguais ao legado, saída sem erro. Sessão 2: checkpoint F5 no passo 7420 e
  SAVE real no slot 3. M3 × Flycast: 97,2 % (amostra e tom) / 95,9 % (com TL, DIPAN, DISDL), 6 inversões; resíduo a analisar.
- **G concluída, saturação caracterizada:** os picos são as 7 portas de efeito juntas (e falas) sobre a música; níveis por
  canal iguais ao original, MVOL 15, clamp de 16 bits no fim, filtro aberto (FLV 0x1FF8) em todos os efeitos, MIXS sem
  proteção como no manual: o original satura igual; nada mascarado. Confirmação acústica com o Flycast inviável (captura
  "disk" do SDL sob o depurador; entrada não exata no combate).
- **Saída do jogo:** fila do XAudio2 que estourava tratada como perda (o mantenedor ficou sem som) → um bloco por VBlank,
  descarte com a fila cheia, seguir a saída padrão (reaberturas), cursor de tocados congelado na perda, reabertura na pausa
  parada, reserva de 66 ms; faltas de dados no H9: 1 399 → 345 (o resto: o jogo abaixo do tempo real, N2).
- Testes: `bs_video_tests` 45/45 e `bs_audio_tests` 79/79 normal e ASan; `bs_unit_tests` 260/260.
- Nada mudou na lógica do jogo; sem promoção do áudio nativo; sem commit.

### N6-F: fala durante a música provada com a gravação do mantenedor (2026-10-21; worktree n6-audio, sem commit)

**Método:** sessão gravada pelo mantenedor na janela (`gravacoes.py`, controle Xbox; LOAD do g1 → combate → cena do Dogs
→ SAVE no slot 2), reprodução headless conferida (estado por passo e saves) e a mesma entrada com `--audio nativo` e
`--audio legado` (logs de áudio, WAV, legendas, estado). Detalhes: `docs/N6-PLANO.md` 18.9.

- Reprodução DETERMINISTICA: 7 915 passos, estado e saves iguais (sem a data de gravação do envelope `.bsv`).
- Fala `e003` com a M2A: ducking pedido pelo jogo no passo do início (−48, rampa 800) e desfeito no estado 3 (−16);
  legenda en-US com as 16 linhas a 0,1–1,0 VBlank do tempo do pacote; estado igual ao legado até o 1º fim natural da
  fala; save final idêntico ao do legado.
- **F:** critério "fala durante a música" provado; "outras músicas no jogo" segue aberto (só a M2A nesta sessão).
- Achados: saturação da mistura no combate e durante a fala (7 795 amostras, 5 estouros do MIXS) → G, comparar com o
  Flycast; sem som na janela (fila do XAudio2 estourada tratada como perda; saída presa ao dispositivo da abertura) →
  corrigido no áudio do jogo, com testes; desempenho do gameplay ~17–18 ms por campo (N2), igual com legado e nativo.

### N6 fase H: preparação, sem promover (2026-10-21; worktree n6-audio, sem commit)

**Método:** bateria nível 2 `tools/dev/n6_h.ps1` (build `n6-game`, ASan `n6-asan`, mods `n6-mods`), revisada pelo
Codex (7 achados, ver Revisões); conferência manual dos arquivos de estado da LOAD.

- Infraestrutura: `--audio nativo|legado`, `BS_AUDIO`, `kAudioNativeDefault` (**legado**, padrão inalterado);
  estatísticas da mistura e da saída (faltas de dados, erros de escrita, perda do dispositivo com troca pela saída
  nula); teste `audio_output_xaudio2_stats`.
- Aprovados: H1 (áudio 79, localização 9, interface 25, vídeo 43; normal e ASan), H2 (efeitos × legado no combate e no
  heliporto), H3 (falas × legado), H5 (seleção, precedência, fallback e valores inválidos), H7 na 1ª versão do critério
  (11 músicas × 300 s sem saturação, sem nota ligada roubada, MIXS 0).
- H4 LOAD tudo nativo × legado: estado lógico igual nos 4 480 passos comuns (o nativo chega 1–2 VBlanks antes em cada
  passo: tempo das cargas para a RAM de som, N2 11.1; o 1º critério comparava a coluna de VBlank e foi corrigido);
  mistura sem saturação, MIXS 0, falhas 0; key-on 3 133/3 151 × Flycast, 0 inversões.
- **Execução interrompida pelo Claude Code por falta de memória da máquina** no H4 do heliporto; H6 (dublagens
  WAV/FLAC/OGG), H8 (controles, filme, saves com áudio nativo), H9 (tempo real) e o H7 com o critério completo **não
  rodaram** e não foram reiniciados sem pedido. Matriz e pendências: `docs/N6-PLANO.md` 20.2–20.3.

### Gravação e reprodução de gameplay (2026-10-21; worktree n6-audio, sem commit)

**Método:** `BS_GRAVAR` na janela com o controle virtual do SDL3 (caminho real dos controles) e uma tecla F5 real;
reprodução headless com `tools/dev/gravacoes.py reproduzir` comparando o estado por passo (`BS_STATE_PASSO`, todo
passo) e os saves; negativo com uma entrada atrasada.

- Gravação por passo de jogo (amostra congelada no passo e quantizada nos bytes do controle), roteiro `indice passo`
  compatível com `BS_INPUT_SCRIPT` e com o oráculo do Flycast; checkpoints (F5); continuação de sessão ou roteiro até
  um passo ou checkpoint; sessão com save inicial identificado (SHA-256), saves produzidos, interface Tk.
- Reproduções determinísticas: NEW GAME 380 passos, LOAD do `g1` + gameplay 3 444, continuação 4 010 (estado igual em
  todos os passos, saves iguais); entrada atrasada 5 passos **acusada** no passo 2247. Detalhes: `docs/N6-PLANO.md` 18.8.
- Nenhuma mudança na lógica do jogo: só a fonte de entrada da janela (filtro opcional) e a ferramenta.

### N6-F (bloqueio da rota) e fechamento técnico da G (2026-10-21; worktree n6-audio, sem commit)

**Método:** busca nas rotas e saves existentes, 5 tentativas no gameplay assistido (áudio todo nativo) a partir do
`passo_heliporto` truncado, comparação de estado por passo, `bs-musica` nas 11 músicas e unitários/ASan.

- **F não concluída: bloqueio.** A primeira fala depois do início da M2A é a cena do Dogs (`e003`), depois do primeiro
  combate; nenhuma rota ou save do repositório passa dele, e o combate não foi vencido às cegas (Eliot com 35 de vida e
  sem arma; morte nas 5 tentativas). Outras músicas: também só depois desse ponto. **Alternativa:** um save do mantenedor
  depois da cena do Dogs (jogo na janela, save nativo) vira uma rota curta de LOAD para medir ducking, sincronia e estado
  (detalhes: `docs/N6-PLANO.md` 18.7). A rota `passo_heliporto` ficou desatualizada (fica no menu de itens; o "game over"
  dos comentários é de outra linha do tempo); o estado é idêntico em todas as combinações de áudio (12 815 passos).
- **G tecnicamente pronta** (encerramento formal depois da F): conjunto único de 48 canais, rampa do envelope no tick,
  MIXS medido (nenhum estouro nas 11 músicas), 11 músicas sem saturação e sem corte de nota ligada, LOAD com estado igual
  e key-on 3 133/3 151; `bs_audio_tests` 79/79 normal e ASan. Limitações: `docs/N6-PLANO.md` 19.2.

### N6 fase G (em fechamento, depois da F) e situação da F (2026-10-21; worktree n6-audio, sem commit)

**Método:** oráculo no Flycast pela rota LOAD (save do oráculo), com `--seguir-sa` (CA, EG, SGC e LP por passo dos
canais que tocam amostras escolhidas); notas públicas de Neill Corlett sobre a AICA e o Flycast como referência de
comportamento (sem copiar código); unitários e a rota LOAD no jogo. Detalhes em `docs/N6-PLANO.md` seção 19.

- **Fase F não concluída.** Pendências registradas: rota com fala durante a música (a `passo_heliporto` termina em
  GAME OVER e as falas das rotas atuais vêm antes da M2A; precisa de rota nova avançando a história), as outras 10
  músicas no jogo e o fade de entrada que depende do tempo do download (N2, 11.1).
- **Oráculo corrigido:** o MSLC (canal monitorado) é o byte alto de 0x280C, não 0x2810; as leituras de monitor antigas
  eram do canal escolhido pelo driver e a leitura de 16 bits limpava o LP que o driver usa (perturbação). Caíram a
  reconstrução antiga do ID 32 pelo `ca` e todo uso antigo do `ca`; as comparações de música (por registradores de key-on
  e TMU), o bend e os ticks não usam o monitor e ficam. O primeiro pacote GDB ganhou prazo (duas sessões presas em
  "Waiting for debugger").
- **ID 32:** com o MSLC certo, o original toca o passo até o fim (LP no LEA → key-off do driver em `0x72D8`): o nativo já
  é equivalente; a afirmação da fase D ("key-off antecipado de 6–15 VBlanks") era artefato de amostragem e foi corrigida.
- **Envelope da AICA implementado** (`native/audio/envelope.h`, na música): registradores da camada nos bytes 0x08–0x0B
  (o parser lia 0x16–0x1F: corrigido); taxa efetiva e tempos decididos pela medida (18 combinações de inclinação do EG na
  M2A batem com a taxa efetiva do Flycast e a tabela de tempos da SCSP/AICA; não com a fórmula das notas de Corlett).
  Soltura real pela RR (RR 16 ~0,6 s; antes 172 ms provisórios), decaimentos até DL. Lei em dB: 0,375 dB por passo de TL
  (o do oráculo); as notas de Corlett dão metade: divergência aberta.
- **48 canais da música** (alocador `0x1244`, parte inferida): livre → em soltura → mais antigo; mixer do jogo a 80 vozes.
  11 músicas (300 s): pico 48, 0 recusadas, 0 saturadas.
- **Efeitos:** os 211 descritores têm AR 31/D1R 0/D2R 0/RR 31: só a soltura de 3,6 ms no `parar` difere (inaudível).
  **DSP:** o MLT carrega SFPB (programa), SFOB e SFPW (128 KB de trabalho, tamanho de reverberação/eco): o uso pelas
  vozes (ISEL/IMXL no registrador 0x20) é o próximo item; o oráculo passou a ler o 0x20 no key-on.
- **Testes:** `bs_audio_tests` **68/68**, normal e ASan, com `BS_GAME_DATA` (0 skips): as medidas de soltura do oráculo
  (±2 %), os estados do envelope e os testes de transição/troca com as soltas reais. Jogo (LOAD): estado e VBlanks iguais ao
  build anterior em 4 481 passos; key-on 3 133/3 151 um a um; mistura sem saturação.
- **Soltura de 3,6 ms dos efeitos implementada** (2026-10-21): o `parar` é key-off, a voz solta pela RR do descritor
  SOSP (+0x0C/+0x0E; os 211: RR 31) linear em dB (mixer: `release`); tocar no mesmo canal corta o que ainda soltava.
  Teste `audio_sd_stop_releases_with_rr`.
- **Reaproveitamento dos 48 canais, medido** (oráculo M2A e LOAD, `tools/dev/n6_canais.py`): 100 % dos key-on da
  música (5 766 e 2 553) caem em canais sem KYONB: o original **nunca corta nota ligada**; no máximo ~39 canais
  ligados ao mesmo tempo. O nativo também não (contador `stolenKeyed` = 0 nas 11 músicas); reaproveita só vozes em
  soltura (M2A: 2 810 em 300 s, 183 delas a menos de 30 dB). Qual canal em soltura o original escolhe: em parte rodízio
  (63 % dos key-on no próximo canal livre depois do anterior), **não provado**; a escolha do nativo (a soltura mais
  atenuada) fica como inferência de efeito pequeno.
- **DSP em uso, medido** (oráculo LOAD, registrador 0x20 no key-on): música IMXL 15/14/13 → MIXS 0 em 97 % dos key-on;
  efeitos IMXL 12–15 → MIXS 0 ou 1. O programa do disco (SFPB, 128 passos, imagem dos registradores 0x3000–0x3BFF; anel
  RBL 3 = 64 K palavras = os 128 KB do SFPW) e as saídas (SFOB: EFREG 0 e 1 a EFSDL 15, pan esquerda/direita) são uma
  reverberação. **Decisão do mantenedor: interpretar o programa.** Implementado em `native/audio/aica_dsp` (só o
  bloco DSP, pela especificação pública); o SFPB é a imagem exata dos registradores a partir do byte 0x54 (704/704
  iguais aos lidos no oráculo com `--dump-dsp`); anel iniciado com o zero do formato. Medidas: impulso com RT60 ~2 s,
  estéreo; M2A com a parte molhada 2,4 dB abaixo da seca, sem saturação; jogo LOAD com efeitos, falas e música nativos:
  estado igual em 4 481 passos, key-on 3 133/3 151. Não provado: o nível molhado contra o console e o envio das falas.
- **Revisão Codex** (`reviews/codex-20261021-020853.md`, 6 achados): aceitos (1) ADRL invertido (manual da AICA, A_SEL;
  o programa do jogo não usa ADRL), (2) os 48 canais por porta → conjunto único das 8 portas (`ChannelPool`), (4) MIXS
  sem proteção de estouro (volta em 20 bits), (6) `jogo = ...` dos pacotes de legendas não conferido no `GameHost`;
  parcial (5) o envelope andava 4 ms antes do primeiro bloco: a voz nova só anda no tick seguinte (o EG em degraus de 4 ms
  fica registrado); rejeitado (3) "deslocamento de negativo é UB": o projeto é C++20, que define o deslocamento de
  inteiros com sinal (P1236); os vetores exatos sugeridos foram acrescentados. Testes novos: `audio_dsp_adrl_source`,
  `audio_dsp_float_vectors`, `audio_dsp_mixs_wraps`, `audio_manatee_synth_shared_pool`.
- **Testes depois das correções:** `bs_audio_tests` **78/78** normal e ASan (com dados, 0 skips), `bs_localization_tests`
  9/9 e `bs_ui_tests` 25/25 (normal e ASan).
- **F, rotas:** `passo_save_rcg02` também tem as falas (`e002a`/`e002b`, com as legendas em inglês ativas) **antes** da
  M2A (VBlank 15 036); `passo_cgate` não toca música. Continua sem rota com fala durante música.
- **Fase G em fechamento:** implementação completa (envelope, soltura, 48 canais, DSP); não é encerrada antes da F
  (prioridade do mantenedor, 2026-10-21: F → G; H só planejada, seção 20 do plano).
- Sem regressão integral (política de 2026-10-20). Sem commit (sem autorização).

### N6 fase F: validação da música depois do marco de integração (2026-10-20; worktree n6-audio, sem commit)

**Método:** oráculo no Flycast com `--ticks` (contador de ticks reais do driver e ponteiro da trilha), `--injetar-musica`
(o carregador do jogo toca a M2C) e linhas `tom` (pitch bend); a mesma rota LOAD no jogo com o save do oráculo
(`n4_vmu.py extrai` + `--saves-importar`); `n6_musica_compara.py`, `tools/dev/n6_ticks.py`; unitários. Detalhes em
`docs/N6-PLANO.md` 18.6.

- **Commits:** `2d73ae2` (OGG/Vorbis) e `ab8f5ed` (N6-F, marco de integração), cada um com a árvore do commit compilada
  e testada à parte. Tempo do download: **opção C**, pendência do N2 (`docs/N2-PLANO.md` 11.1), com a consequência
  audível medida (o original faz fade de entrada na troca de música por causa da espera do DMA; o nativo não).
- **Pitch bend (M2C):** lei lida no driver (faixa dos bytes 0x0C/0x0D do grupo, 2 semitons) e provada no Flycast: cada
  leitura do mergulho do canal 0 bate com exatamente um valor do bend nativo, nas 4 vozes, até −2 semitons e de volta.
- **Mesma rota que o oráculo (LOAD):** comandos idênticos; sem fade de entrada (porta já em −16); 3 133/3 151 key-on um a
  um. **M2C:** 1 859/1 877 em 100 ms (os 18 sem par são o fade de entrada do original).
- **Ticks perdidos: medidos** (antes hipótese): 940/944 leituras do oráculo têm o mesmo (ponteiro, contagem) no
  sequenciador nativo (pareamento exato); o atraso fica em 0/+1 tick e sobe para +6/+7 no trecho denso da M2A (~15–21 s):
  6 ticks (24 ms) perdidos pelo driver, no modelo de ciclos do Flycast. Sem compensação no nativo.
- **Ducking:** comandos do jogo nas falas (−48/−16 t=800; −47/−16 t=2000) e teste com a M2A: −12,00 dB durante, igual
  antes e depois. Falta uma rota com fala durante a música.
- **11 músicas:** todas tocam e laçam (1ª volta entre 113 e 183 s), 0 recusadas; as 131 notas sem camada em 600 s são as
  que o driver também não toca; transições pela ordem do carregador nas 11. No jogo, só a M2A (as rotas não chegam às
  outras áreas).
- **Codex** (`reviews/codex-20261020-232634.md`, 5 achados aceitos e corrigidos): ticks por (ponteiro, contagem) no
  próprio `n6_ticks.py`; linhas `tom` com a mesma amostra; bend conferido no tom efetivo do mixer e reset sem evento de
  pitch; transições com vozes e energia por música; sem posição depois do fim da sequência.
- **Unitários:** `bs_audio_tests` **67/67**, normal e ASan, com `BS_GAME_DATA` (0 skips). Jogo com o bend: estado,
  VBlanks e vozes idênticos ao build anterior na rota LOAD (4 481 passos).
- Fase F **não concluída**: faltam rota com fala durante a música e as outras músicas no jogo. Sem regressão integral.

### OGG/Vorbis de mods: libogg 1.3.6 + libvorbis 1.3.7 (2026-10-20; decisão do mantenedor)

**Método:** tarballs oficiais da Xiph conferidos com os `SHA256SUMS` (libogg `83e67047…4638`, libvorbis `0e982409…5ab`);
os 49 arquivos copiados comparados byte a byte com os dos tarballs (iguais; só `config_types.h` escrito aqui, tipos de
`<stdint.h>`); testes `test_audio_ogg.cpp` (arquivo próprio de 4 KB embutido). Detalhes em `third_party/xiph/README.md`
e `docs/N6-PLANO.md` seção 18.1.

- Só a decodificação (sem `vorbisenc`), compilada pelo nosso CMake (`bs_xiph_vorbis`, avisos de terceiros desligados;
  MSVC e GCC/Clang), no kit do Builder e no projeto exportado (`kKitEntries`): compila sem internet. Licenças BSD-3
  (`COPYING`) copiadas para `licencas\` pela `make_builder.ps1`.
- `parse_ogg` (`native/audio/sound_vorbis.cpp`): só da memória; um fluxo lógico; canais 1–2; taxa 8–192 kHz; duração e
  orçamento de PCM conferidos antes de decodificar; buracos do fluxo limitados; falha de alocação tratada.
- Testes: decodificação (duração, nível, ~440 Hz), entradas ruins, 300 mutações (troca, corte, repetição) sem queda;
  com o ASan, nenhum acesso fora dos limites nesses casos (não é prova para entradas arbitrárias). WAV e FLAC iguais.
- `bs_audio_tests`, na árvore deste commit: ver a mensagem do commit.

### Legendas em inglês por mod: inventário, transcrição local e pacote demonstrativo (2026-10-20; worktree n6-audio, sem commit)

**Método:** `tools/dev/legendas/` (catálogo, transcrição com faster-whisper large-v3 na GPU num venv isolado, geração
do pacote, prévia, testes); dados de trabalho em `%LOCALAPPDATA%\BlueStingerRecomp\legendas-en` (fora do Git).
Detalhes em `docs/LEGENDAS-EN-PLANO.md`.

- **Inventário:** 239 IDs, 233 WAV (um a um), 1 421 s; pela detecção de voz, 200 com fala e 32 sem fala; `E084`
  estéreo bloqueado (também é uma pendência do N6). O decodificador do pipeline é igual ao do jogo (impressão FNV do
  PCM em Python e em C++).
- **Transcrição:** 685 linhas das falas (602 automáticas, 79 a revisar, 4 excluídas: incompreensíveis ou alucinação
  do modelo) e 45 linhas em 8 filmes; nomes pelo texto do jogo (Eliot G. Ballade, ESER, Kimra, Kimratech).
  **0 linhas revisadas por humano**: a transcrição automática não é dada como correta.
- **Pacote** `mods\legendas-en` no formato do `SubtitleService` (e os filmes também no leiaute do N5): carrega no parser
  do jogo sem problemas; no jogo, com `--audio-falas nativo --legendas en-US`, as linhas da `E002A` e da `E002B`
  ficam ativas nos tempos certos (no log; o desenho das falas é do N7). Os filmes já são desenhados pelo player do N5.
- **Testes:** `test_legendas.py` 11/11; `bs_audio_tests` 41/41; `bs_localization_tests` 8/8; `bs_video_tests` 42/42.

## Revisões independentes (Codex)

### 2026-10-22: N7b, segunda rodada. Relatório em `reviews/codex-20261022-000105.md` (5 achados)

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Falha de pipeline omitia o lote e o quadro era aceito | **Aceito** | `pipeline()` nulo devolve erro e o render vai para o software ou para a recuperação |
| [ALTA] Ambiente de ponto flutuante não fixado; `crdiv` dependia de FMA fundida | **Aceito, e além do sugerido** | Divisão corretamente arredondada por aritmética **inteira** das mantissas; tabela exata de `c/255`. Variantes SPIR-V com `RoundingModeRTE` e com `RoundingModeRTE` + `DenormPreserve`, escolhidas pelas propriedades reais da GPU; o log informa as garantias |
| [MÉDIA] Sem rasterização conservadora o shader exato perdia pixels | **Aceito, com outra solução** | Candidatos pelo **retângulo envolvente** do software (independente do raster da GPU); a extensão saiu |
| [MÉDIA] O deslocamento das profundidades negativas não é injetivo | **Aceito** | Profundidade crua com `VK_EXT_depth_range_unrestricted` (NVIDIA e AMD); sem a extensão, o deslocamento com o limite informado. O contraexemplo do relatório virou cena de teste, idêntica nas duas GPUs |
| [MÉDIA] O teste de imagem aceitava PSNR em vez de igualdade | **Aceito** | Todas as cenas do modo fiel exigem 0 pixels diferentes |

Também nesta rodada:
- **`resolve.comp`:** ordenação local até 128 camadas e seleção acima. Passadas de 64 em 64 davam
  imagem errada na AMD, então não são usadas.
- **Robustez:** `robustBufferAccess` e índices protegidos contra NaN.

### 2026-10-21: N7b, primeira rodada. Relatório em `reviews/codex-20261021-231515.md` (6 achados)

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Falha de textura gerava quadro errado em vez do software | **Aceito** | Textura não enviada deixa o render no software |
| [ALTA] Resultados de alocação e mapeamento ignorados | **Aceito** | Todos conferidos; buffer sem mapeamento é falha |
| [MÉDIA] `--render nativo` ignorado com `--tempo legado` | **Aceito** | A instalação da captura saiu do bloco do tempo |
| [MÉDIA] `HALF_OFFSET` bit 1 não chegava ao Vulkan | **Aceito** | O modo fiel usa os pesos na amostra dos atributos, como o software |
| [MÉDIA] 1/w negativo era cortado pelo clamp | **Aceito** | Ver a segunda rodada (profundidade crua) |
| [MÉDIA] Testes não comparavam a imagem inteira | **Aceito** | Imagem inteira contra o software; falhas injetadas |

### 2026-10-21: N7a (captura do TA na IR, contrato das texturas HD). Relatório em `reviews/codex-20261021-220352.md` (7 achados)

| Achado | Decisão | Evidência / ação |
|---|---|---|
| [ALTA] Globais iguais de volume modificador eram fundidos num volume só | **Aceito** (e corrigido além do sugerido) | O volume fecha pela instrução do global (0 polígono, 1 inclusão, 2 exclusão), não pela troca de palavras. O sink recebe cada global (`PvrCaptureSink::global`). Teste `render_capture_modifier_volumes_close_on_last_polygon` (globais iguais, inclusão, exclusão, sem fechamento). Na rota: 76.290 volumes (antes, 153.514 pela regra errada), 1.173 sem último polígono |
| [MÉDIA] Nomes de `Cull` 2/3 invertidos | **Aceito** | O rasterizador descarta área < 0 (anti-horário na tela, y para baixo) com 2. Enum reordenado; teste `render_capture_cull_names_follow_the_rasterizer` desenha um triângulo horário com 2 e 3 |
| [MÉDIA] Round-trip perdia filtro 3, mip bias, supersample e SRC/DST select | **Aceito** | `Filter` com 4 códigos; os campos foram adicionados ao `DrawState`. O teste confere bit a bit todos os bits do TSP fora do tamanho (cada bit isolado e 64 combinações). Única exceção: flip com clamp não tem efeito, porque o clamp prevalece no rasterizador |
| [MÉDIA] `FrameParams` perdia metade da tabela de fog e não tinha `FOG_COL_VERT`/`FOG_DENSITY` | **Aceito** | Tabela de 16 bits por entrada e os dois registradores guardados em bruto. O uso de fog pelo jogo ainda não foi verificado (o software não aplica fog) |
| [MÉDIA] A imagem "a partir da IR" relia os registradores vivos | **Aceito** | `PvrFrame`: o rasterizador lê os registradores do quadro uma vez. `pvr_render_triangles` recebe `pvr_frame(Frame)`, convertido dos `FrameParams`, e o modo conferir exige `pvr_frame(Frame) == registradores usados`. Teste `render_capture_image_from_ir_ignores_live_registers` muda os registradores depois do render e a imagem não muda. Os texels continuam vindo da VRAM (conferidos à parte pelas conversões nativas) |
| [BAIXA] Sink instalado depois de sessão sem render; estatísticas cumulativas | **Aceito** | `install(Off)` sempre no fim da sessão; `install` recria a captura (estatísticas, quadro e ids) |
| [BAIXA] IR inválida ainda era percorrida | **Aceito** | `valid(Frame)` = `validate` + handles de textura dentro da tabela; se falhar, a conferência para |

Depois das correções: unitários 269 de 269 e 18 de 18; rota de combate com 0 divergências e quadros idênticos à linha de base.

### 2026-10-21: critério de saturação do H4 e orçamento do `test_saves`. Relatório em `reviews/codex-20261021-190156.md` (2 achados)

- [MÉDIA] o limite simétrico contava `-32768` (PCM16 válido) como fonte acima de 16 bits: **aceito**. Faixa assimétrica
  depois do arredondamento da saída (≥ 32767,5 ou ≤ −32768,5); testes do extremo negativo com ganho 1 (válido) e 1,01 (acima).
- [MÉDIA] o critério não detecta nível por canal mais alto que o original abaixo de 16 bits: **aceito como limite
  documentado**. O H4 fica descrito como detector de clipping introduzido por fonte; a igualdade de nível vale nos
  registradores (Flycast), sem prova acústica (`docs/N6-PLANO.md` 20.2).
- Verificado sem problema: orçamento de 46 000 suficiente para S1, S2 e S17; o roteiro por passo inalterado; sem efeito na
  lógica do jogo; a contagem por fonte depois de volume, pan e soltura, e a do DSP depois de EFSDL/EFPAN.

### 2026-10-21: reserva de 66 ms do XAudio2 e correções posteriores. Relatório em `reviews/codex-20261021-155015.md` (7 achados)

- [MÉDIA] a reserva atrasa o áudio audível ~66 ms em relação ao relógio lógico (legendas, animações) e a sincronia da F é
  medida no headless: **aceito como latência conhecida**. A reserva fica (as faltas de dados caem 75 %); o atraso está
  documentado (`docs/N6-PLANO.md` 20.3) como dado de entrada do N7, que desenha as legendas das falas no gameplay (hoje só
  registradas) e terá de compensar a fila da saída; a sincronia da F vale para o relógio lógico.
- [MÉDIA] o teto de 200 ms não limitava o `write()` (o descarte começava em 4×): **aceito**. O teto do contrato vale para a
  fila, contando a reserva e o bloco novo; teste `audio_output_xaudio2_follow_burst` (fila máxima ≤ 200 ms).
- [MÉDIA] o critério de saída da F aceitava descartes: **aceito** (descartes = 0).
- [MÉDIA] a base do ducking podia vir de outra porta ou de chamada recusada: **aceito** (uma sequência só, porta 0, r0 0).
- [MÉDIA] a escolha do pacote de legendas não validava como o jogo: **aceito** (formato 1..1, id válido e único, `jogo` =
  "T13001N V1.000", idiomas canônicos, como `ModPackages::discover`).
- [BAIXA] o estado podia passar sem cobertura: **aceito** (passos contínuos desde o início até o 1º fim natural).
- [BAIXA] `.bsv` com tamanho que o jogo recusa era normalizado: **aceito** (imagem > 0, múltipla de 512, ≤ 200 blocos).
- Reanálise das duas sessões com o analisador corrigido: sessão 2 todos os critérios; sessão 1 todos menos "outra música"
  (esperado): as evidências da F continuam de pé.

### 2026-10-21: saída do jogo (seguir a saída padrão), gravações e análise da F. Relatório em `reviews/codex-20261021-145415.md` (9 achados)

- [ALTA] falas/ducking aprovavam sem fim natural nem volta do volume: **aceito**. Episódios por fala (do início ao próximo
  início); ducking = descida em relação ao pedido anterior, na porta da música, com r0 0, e volta a essa base no estado 3;
  exige ao menos uma fala com fim natural.
- [MÉDIA] `played_frames` regredia na reabertura depois de erro crítico: **aceito**. Cursor congelado no último valor lido
  com o motor saudável; `XAUDIO2_VOICE_STATE` zerado.
- [MÉDIA] música identificada sem o slot/banco: **aceito**. Slot → arquivo a cada carga, em ordem; o tocar pelo banco; os
  M*.MSB têm tamanhos únicos (só M3.MPB e M9.MPB colidem, e o banco de amostras não decide a música).
- [MÉDIA] legendas: todos os arquivos e cobertura parcial: **aceito**. O mesmo pacote que o jogo (idioma exato antes do
  recuo, prioridade, id), chave voz|cena, linhas faltando ou sobrando reprovam.
- [MÉDIA] a F passava com erros ou perdas da saída: **aceito**. `saida_sem_erro_nem_perda` (erros de escrita e perdas = 0);
  descartes informados.
- [BAIXA] reabertura na pausa iniciava a voz: **aceito** (`start()` respeita a pausa).
- [BAIXA] normalização de `.bsv` sem validar o envelope: **aceito** (só v1 com cabeçalho, tamanho e CRC-32 conferidos).
- [BAIXA] janela sem passos virava "igual": **aceito** (`JANELA_SEM_COBERTURA`).
- [BAIXA] testes não exercitavam a reabertura: **aceito** (`audio_output_xaudio2_follow_reopen`, com saída padrão, relógio e
  perda injetados).
- Correção minha depois da revisão: "sem camada" deixou de reprovar a F e o H7 (dado da música, o driver também não toca).

### 2026-10-21: infraestrutura da fase H do N6. Relatório em `reviews/codex-20261021-121034.md` (7 achados)

- [ALTA] a bateria terminava com 0 sem executar provas obrigatórias (skip não reprovava): **aceito**. Pré-requisito
  ausente (dados, save g1, oráculo, ffmpeg, ASan, build com mods) reprova; `-Parcial` aceita o skip e avisa que a
  execução não vale para a promoção.
- [ALTA] faltavam cenários do próprio plano (heliporto e saves com áudio nativo): **aceito**. H2 roda os efeitos nas
  rotas de combate e do heliporto; H4 compara tudo nativo × tudo legado na LOAD e no heliporto; H8 roda o
  `test_saves.ps1` com `BS_AUDIO=nativo` (exige o build com mods, `build\n6-mods`).
- [ALTA] H7 aprovava música com falha de síntese ou sem música nenhuma: **aceito**. Inventário das 11 músicas e, por
  música, notas e vozes > 0 e zero em sem camada, recusadas, tom saturado, falhas de amostra, saturação, nota ligada
  roubada e MIXS.
- [ALTA] a perda do dispositivo deixava o jogo mudo e o H9 passava: **aceito**. O adaptador confere a escrita: com a
  saída não saudável, troca pela nula com log (o contrato de `IAudioOutput`); `Stats` ganhou erros de escrita e perda,
  somados no resumo (`erros de escrita`, `perdas do dispositivo`); H9 exige zero.
- [MÉDIA] pausa/reabertura contavam errado: **aceito**. `open` zera o estado da sessão (preparo, pausa, perda,
  estatísticas, tocados); o descarte durante a pausa não religa a voz. Teste `audio_output_xaudio2_stats` (falta só
  com a fila vazia depois do primeiro bloco, descarte em pausa, erro de escrita com a saída fechada, reabertura zerada).
- [MÉDIA] variáveis herdadas (`BS_SEM_DSP`, `BS_AUDIO`) mudavam os casos: **aceito**. Todo o ambiente `BS_*` de quem
  chama sai durante a bateria e volta no fim; cada caso começa limpo.
- [MÉDIA] o critério do oráculo ignorava vozes extras e inversões: **aceito parcialmente**. Exige o código de saída do
  comparador, ≥ 99 % pareados no critério mais estrito e **0 inversões**; as vozes extras não são zero no resultado
  aceito (47: a primeira leitura do oráculo 736 ms depois do tocar e o recálculo estéreo pelo controle 7, seção 18 do
  plano), então o teto é 2 % dos pareados, não zero.

### 2026-10-21: gravação e reprodução de gameplay. Relatório em `reviews/codex-20261021-112540.md` (7 achados)

- [MÉDIA] a reprodução seguia até 600 VBlanks depois do fim, e os saves eram comparados depois dessa cauda: **aceito**.
  O rodapé grava o passo e o VBlank em que o jogo parou (`# fim passo P vblank V`), e a reprodução headless para no
  mesmo VBlank (o jogo para depois do gancho desse VBlank nos dois modos). Reprodução: passos além do fim = 0.
- [MÉDIA] durante filmes a entrada fica congelada (não dá para pular o filme gravando): **aceito como limitação, sem
  mudança**. É determinística (a reprodução vê o mesmo), está documentada (`docs/N6-PLANO.md` 18.8) e não impede a
  gravação; um índice por (passo, leitura) exigiria mudar o formato e o roteiro do Flycast, sem necessidade para a F.
- [MÉDIA] o oráculo do Flycast recusava valores intermediários do analógico e dos gatilhos (`AX=79`, `LT=93`):
  **aceito**. `roteiro_bu` aceita AX/AY/LT/RT/A2X/A2Y = 0..255 e escreve x1/y1 (eixo − 128) e R/L no PDS como o
  decodificador do SDK (gatilho como botão a partir de 128; A2X/A2Y ignorados, o controle padrão não tem o segundo
  analógico). Roteiros só digitais seguem idênticos; valor inválido é recusado.
- [MÉDIA] uma gravação truncada podia ser rotulada determinística: **aceito**. Vereditos DETERMINISTICA / DIVERGENTE /
  INCOMPLETA: exige rodapé, `rc` 0 da gravação e estado contínuo até o fim; `BS_STATE_PASSO` faz flush a cada 64
  passos. Negativo: sessão sem rodapé → INCOMPLETA (o trecho existente ainda é conferido).
- [BAIXA] `DESCONECTADO` no roteiro base teria outra semântica na gravação (a presença não passa pelo filtro):
  **aceito**. Recusado ao abrir (`rc` 2 com a mensagem).
- [BAIXA] o BMP do checkpoint e o passo anotado não são um instantâneo atômico: **aceito parcialmente**. O checkpoint
  sai depois de o quadro da iteração ser atualizado; o passo anotado (o que vale para `--ate cpN`) é o do gancho de
  VBlank, e o BMP fica documentado como ilustrativo (pode ser de um VBlank antes).
- [BAIXA] o teste não provava o congelamento nem a equivalência gravador × roteiro: **aceito**. A lógica foi para
  `transitional/input_bridge.h` (`SessionRecorder`) e o teste `input_native_session_recorder` cobre leituras repetidas no
  passo, passos sem leitura, troca exclusiva no passo de `BS_GRAVAR_ATE`, gatilho intermediário, a ida e volta pelo roteiro
  reproduzido e a recusa de `DESCONECTADO`.

Retestes (2026-10-21): `bs_unit_tests` 260/260 normal e ASan; LOAD do `g1` com analógicos intermediários e F5 (3 094
passos), continuação do `cp1` (3 762 passos, prefixo idêntico): DETERMINISTICA, saves iguais, parada no mesmo VBlank;
negativo (ANALOG_UP atrasado 4 passos) acusado no passo 2224; sessão sem rodapé INCOMPLETA; base com `DESCONECTADO`
recusado.

### 2026-10-19: N4 (saves nativos), duas rodadas. Relatórios em `reviews/codex-20261019-140151.md` e `-143748.md`

- Primeira rodada:
  - [MÉDIA] FF03 inalcançável: **aceito**. `card_code` separa ausente de não formatado; unitário
    com store falso.
  - [MÉDIA] unidade liberada antes dos progressos: **aceito**. `tick(hold)`/`release`: ocupada (−1)
    durante os progressos, como no oráculo c2, e liberada logo antes do callback de conclusão;
    unitário.
  - [MÉDIA] CRC do VMS não conferido na gravação: **aceito**. `vms_check` antes do temporário e na
    releitura; unitário.
  - [MÉDIA] versão futura com cabeçalho curto virava `.corrompido`: **aceito**. A versão é lida antes
    do tamanho do cabeçalho; unitário.
  - [BAIXA] `init`/`exit` incompletos: **rejeitado**. O `exit` não tem chamador. O `init` nativo não
    falha (o caminho de sucesso é o do original), e `r4`/`r6` (capacidade e trabalho) não têm uso
    num store de arquivos.
  - [BAIXA] S6 contra o padrão: **resolvido** pela promoção.
- Segunda rodada:
  - [MÉDIA] exportar podia sobrescrever o próprio `.bsv`: **aceito**. Destino igual ao
    `.bsv`/`.bak`/`.tmp` do slot é recusado; a exportação grava por temporário e troca.
  - [BAIXA] cartão não formatado sem inserção: **aceito**. A ordem exata no hardware não foi
    verificada.
  - [BAIXA] headless sem pasta não testado: **aceito**, com o S14.
  - [BAIXA] importar e exportar juntos: **aceito**, recusado.
- Achado próprio, nos testes: o Windows (`ReplaceFileW`) troca um save somente leitura. O store
  agora devolve `ReadOnly`; unitário.

### 2026-10-18: N3-3 e planejamento do N4

- N3-3 (`reviews/codex-20261018-224640.md`), todos aceitos:
  - [MÉDIA] negativo do P9 sem conferir a execução: código de saída e divergência real exigidos;
  - [MÉDIA] perfis com desconexão e troca não exercitados: `DESCONECTADO` no roteiro físico, P9 de
    conexão e unitário de hotplug;
  - [BAIXA] perfil fora do evento de desconexão: corrigido.
- N4, só planejamento (`-220833.md` inventário, `-225209.md` revisão arquitetural): incorporados em
  `docs/N4-PREPARACAO.md`, com as decisões que ficam com o mantenedor.

### 2026-10-18: N3-2 (provedor nativo do pd). Relatórios em `reviews/codex-20261018-182417.md` e `-220359.md`

- [MÉDIA] coleta não amarrada ao fim real do DMA: aceito em parte. O fim exato exigiria emular o tempo
  do barramento do controle. Entrou o invariante "uma passada do servidor por disparo", cobrado na
  suíte; a limitação residual está documentada.
- [MÉDIA] unidades ≠ A0 com resposta forçada: aceito; a descrição bruta da camada `per` (`8C08F4B6`).
- [BAIXA] soft reset sem conferência semântica: aceito.
- [BAIXA] SKIP com sucesso: aceito (código 2).

2ª rodada: as quatro correções confirmadas; [MÉDIA] janela temporal residual aceita como limitação explícita; [BAIXA] faltava unitário da unidade-base B0 pela descrição bruta: aceito (235 unitários). Rejeitados: nenhum.

### 2026-10-18: N3-1 (núcleo nativo de entrada). Relatório em `reviews/codex-20261018-120344.md`

Aceitos e corrigidos (com testes):
- [MÉDIA] controle recém-conectado e neutro tomava o jogador 1: conectar não é mais atividade;
- [MÉDIA] perder o foco limpava os controles SDL, e o SDL não reenvia o botão mantido: o foco solta
  só o teclado (`release_keyboard`);
- [BAIXA] latch também em DEVICE_INFO/ALL_STATUS: `InputSource::connected`, uma amostra por
  GET_CONDITION (teste com DMA de dois quadros);
- [BAIXA] `misc2`..`misc5` rejeitados no TOML: aceitos, e também `misc6`, que existe no SDL3
  fixado.

Rejeitados: nenhum.

### 2026-10-17: N3-0, duas rodadas. Relatórios em `reviews/codex-20261017-164009.md` e `-173236.md`

1ª rodada, todos aceitos e corrigidos:
- [MÉDIA] o negativo do VBlank-In divergia por construção (lia antes de avançar o índice). A
  transição injetada passou para o primeiro acesso ao MDST, que fica entre a entrega do VBlank-In e
  o disparo do DMA. A ordem foi conferida no binário (8C08F27C → 8C08F332 → 8C0976DE, depois 8C0975C0)
  e no runtime;
- [MÉDIA] K13 sem `-ExeRef` passava em silêncio. Agora é obrigatório e cobre 3 rotas;
- [MÉDIA] x2/y2 e os padrões de `old` não eram exercitados. Entraram `A2X`/`A2Y`, a matriz 2 com
  `support` completo e a cobertura de `old` na sonda;
- [MÉDIA] a conclusão sobre os consumidores excedia a prova. Agora há passadas conservadora e
  otimista com revisão manual, a K11 compara o estado do jogo a cada passo e a conclusão vale só
  para as rotas;
- [BAIXA] K5 não testava determinismo: duas execuções por build, mais a paridade;
- [BAIXA] o conversor citava K9 (o caso é K12) e o relatório não tinha os resultados.

2ª rodada:
- [BAIXA] o comparador aceitava passos de um lado só. Agora exige os mesmos passos e cabeçalhos;
- [BAIXA] resultados no relatório (registrados).

Nenhum rejeitado.

### 2026-10-17: plano do N3 (controles nativos). Relatório em `reviews/codex-20261017-145645.md`

Aceitos:
- [ALTA] o init `8C15C10C` não pode ser substituído (chama o init `per`/Maple `8C08F0E0`,
  conferido em `8C15C180-198`);
- [MÉDIA] o ponto de amostragem é o MDST no tratador `8C08EFF8`, não o VBlank-In;
- [MÉDIA] `BS_MAPLE_PORTS` não serve de oráculo de desconexão (lido uma vez);
- [MÉDIA] o produtor de `info`/`name` no modo nativo;
- [MÉDIA] a cobertura "100%" do contrato;
- [BAIXA] limiares e quantização;
- [BAIXA] artefatos auditáveis (`analysis/n3/`).

Rejeitados: nenhum.

### 2026-10-17: gameplay assistido e comparação por passo. Relatório em `reviews/codex-20261017-110130.md`

| Achado | Decisão |
|---|---|
| [MÉDIA] `pad` depois de um `roteiro` com entradas futuras era adiado até a última entrada e a substituía | **Aceito.** `pad` recusado enquanto houver entradas futuras (erro com o passo); caso A8b. Nenhuma sessão gravada caiu no caso |
| [MÉDIA] A1/A4 comparavam só a interseção dos passos: um buraco num log passaria | **Aceito.** Todos os passos da faixa e todas as amostras múltiplas do intervalo são exigidos; negativo A6b (passo removido de um log) |
| [BAIXA] cargas de sala no mesmo passo só eram impressas | **Aceito.** Veredito `cargas` (passo, arquivo), caso A7 |
| [BAIXA] `pad_state_text` não é sem perda para valores intermediários | **Aceito.** Contrato restrito ao subconjunto digital (documentado em `input.h` e no unitário) |
| Não verificado: sessão ao vivo = reprodução sem prova automatizada | **Aceito.** Caso A8 (sessão ao vivo pelo cliente e reprodução da `sessao.txt`, quadros VBlank a VBlank) |
| Não verificado: deslocamento 6 e endereços de RAM só como afirmação | Método documentado em `docs/N2b2-GAMEPLAY.md` (1.2, 1.4); endereços só de diagnóstico |

Verificado pelo Codex sem problema: o passo de jogo está na saída da espera (`bt 8C064A26` em
`8C064A2C`, único chamador `8C078664` no laço principal); o contador conta nos dois modos sem mudar a
ordem de `progress()`/`wait_done()`; roteiros antigos continuam por leitura; `Assist` só existe com
`BS_ASSISTIDO` e headless; nada de emulação em `assist.cpp`.

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
