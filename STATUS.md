# STATUS

Cada item registra **o que** foi verificado, **como** e **quando**.

## Marcos

| Marco | Estado |
|---|---|
| M0 esqueleto, README, LEGAL, ARCHITECTURE | concluído (decisões D1–D6 registradas em 2026-10-13) |
| M1 extrator + hash | concluído, aguardando revisão |
| M2 decodificador SH-4 + testes | — |
| M3 descoberta de funções + C++ que compila | — |
| M4 boot até o primeiro frame | — |
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

## Em aberto

- Confirmar se `BSDATA/R*.BIN` contêm código SH-4 (risco R3, início do M3).
- D7 (distribuição), decidir no M8.
- clang-cl não está instalado nesta máquina. Por enquanto só o MSVC é testado.
