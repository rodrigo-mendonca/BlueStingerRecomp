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

## Revisões independentes (Codex)

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
