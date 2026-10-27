# N8.5: filmes e CRI legado

**Situação (2026-10-27): concluído.** Executado pela autorização contínua do mantenedor até o fim do N8 (2026-10-27: "Pode
avançar até o final do n8 sem pedir autorização"). Referência imutável do N8.4: o commit `d4fc4c8`.

## 1. Inventário (somente leitura)

### 1.1 O caminho do filme nativo (o padrão desde o N5)

A função de vídeo do jogo (`8C08B544`) chama as entradas MW, atendidas pelo `transitional/video_adapter`: create,
parâmetros, cores, start, stop e estado. Medido com o build `mods` (`BS_CALL_TRACE`), `passo_newgame_area1`, 6 000
VBlanks:
- da biblioteca Sofdec, só roda a **init** (`mwPlyInitSofdec`, `8C158B70`, chamada no boot por `8C0788A0`): é código
  recompilado de inicialização;
- o adapter registra 1 create e 1 start, 1 949 consultas, **0 chamadas de outros players** e **0 entradas expostas ao
  servidor**;
- **nenhuma leitura de `.SFD`**: o registro de arquivos das 16 referências tem 0, e o do NEWGAME também;
- **nenhum acesso ao conversor YUV do TA:** a sonda `BS_HW_PROBE` do N8.3 mostra as escritas do TA só em `0x1000xxxx`
  (polígonos), nada em `0x108xxxxx` nem em `TA_YUV_TEX_*`.

### 1.2 O que ficava no produto

| Item | Uso |
|---|---|
| `--video legado` | o Sofdec original (código recompilado do MW): lê os `.SFD` por gdFs, decodifica MPEG-1 e ADX e sobe o YUV pelo conversor do TA |
| conversor YUV do TA (`ta_yuv_*`, `TA_YUV_TEX_BASE/CTRL/CNT`) | só o filme legado |

**Fica:**
- o código recompilado do Sofdec (AOT, até o N10): no produto, fora a init, só é alcançável pelas entradas MW, que são
  sempre do adapter nativo;
- o conversor de vídeo do Builder (`builder/video`: SFD para MP4, ferramenta);
- o catálogo e os mods de filme (`native/video`);
- o oráculo (`tools/dev/n5_oraculo.py`).

## 2. Decisão

A opção recomendada, a mesma do N8.3/N8.4 (autorização contínua):
- o provedor nativo é o único do produto;
- o `--video legado` só existe na validação;
- o conversor YUV é componente removido no produto: escrita dos registradores, leitura do contador ou macrobloco pela
  FIFO são erro explícito.

Alternativa descartada: slots na init do Sofdec (`8C158B70`). Mudaria o código gerado e a RAM do boot, o que é proibido
(AOT intacto) e quebraria as referências.

## 3. Arquitetura

- **`GameHost`:** no produto, `VideoProvider::Legacy` é recusado (erro). O `main_sdl` recusa `--video legado` com
  saída 2 e mensagem;
- **`VideoProvider::None`:** sem adaptador, só para os jogos falsos dos testes;
- **guarda do YUV no produto** (`#if !BS_VALIDACAO`, `pvr/ta.cpp` e `pvr/pvr_regs.cpp`): `ta_yuv_removed` dá o erro
  explícito. A FIFO de polígonos e o resto do PVR ficam como estão, até o N8.7;
- **suítes:** as que dependem da linha do tempo do Sofdec vão para o exe de validação, conferido pelo `valida_exe.ps1`
  (`test_assistido`, `test_ordem`, `n5_e001`; o H4–H6 do `n6_h`). As que testam o produto mudam para o filme nativo:
  - **`test_gameplay`:** o roteiro por passo das referências. Pontos medidos com o filme nativo: o HUD em 37 750;
    movimento entre 34 000 e 35 000; sem controle a partir do passo 2630. Os critérios são os mesmos (HUD com mais de
    40 pixels verdes, movimento acima de 10, parado com menos da metade);
  - **`test_projeto`:** o exe exportado roda com o filme nativo. Sem os MP4 do `--sem-filmes`, o filme fica sem recurso
    e o jogo segue.
  - **mod de exemplo do projeto exportado** (`builder/project_export.cpp`): o alvo era `8C194580`, um método
    `rts; mov #0,r0` de uma tabela da biblioteca CRI. A tabela vem logo depois da string "1.04 ... CRI" em
    `8C1DBF58`. Com o filme nativo ele nunca é chamado, o que é prova extra de que o Sofdec ficou inalcançável. O
    exemplo passou a contar a função de vídeo do jogo, `8C08B544`, chamada no NEW GAME (VBlank 2 095 do roteiro do
    teste). Assim o `test_projeto` confere o mesmo critério ("mod em C++ ativo") na mesma janela de 2 300 VBlanks.

## 4. Revisão independente (Codex, `codex-20261027-035102`)

| Achado | Decisão |
|---|---|
| [MÉDIA] a V4 do `n5_e001` não exigia zero chamadas encaminhadas ao MW original | **aceito**: a V4 exige `chamadas de outros players 0` (e entrada exposta 0, falhas 0); o `n5_e001` passou 10/10 com a regra nova |
| [BAIXA] o `n5_e001` aceitava o exe do produto | **aceito**: `Assert-Validacao` |
| [BAIXA] faltavam os testes da recusa do `GameHost` e do boot com o estado do PVR | **aceito**: `game_host_product_refuses_legacy_video` e `n85_product_boot_pvr_state_does_not_trip_yuv_guard`: o `pvr_regs_load` com os registradores YUV preenchidos não dispara a guarda; a leitura do guest depois continua recusada |

## 5. Equivalência e testes (2026-10-27)

**Exe final do produto** (`6186D377…`, o mesmo das referências): 16/16, com as 179 exceções de SPC e nenhuma nova.

**Validação:** todas as mudanças de código do N8.5 estão em `#if !BS_VALIDACAO` ou nos scripts. O
`VideoProvider::None` é só dos testes. Por isso a validação se comporta como no N8.4, e as suítes do legado do N8.4
(`test_pd_nativo`, `test_saves`, `test_n8_nativo`, `test_n2b2`) continuam valendo.

| Teste | Resultado |
|---|---|
| unitários | 329/329 no produto e na validação |
| ctest Debug | 19/19, com e sem o disco |
| catraca | produto 35, validação 42; recusa do `--video legado` |
| `test_assistido` (validação) | 28/28 |
| `test_ordem` (validação) | 8/8 |
| `n5_e001` (validação, V4 estrita) | 10/10 |
| `test_gameplay` (Preview, filme nativo) | 4/4: HUD 557 pixels verdes; movimento 136,7/138,2 × parado 4,5 |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 (com o mod de exemplo novo; a 1ª rodada, com o alvo do Sofdec, deu 17/18) |
| `test_preview` | 12/12 |
| H9 | 0 faltas, 0 erros, 0 perdas |

**Tamanho:** o produto tem 19 244 032 bytes (N8.4: 19 244 544).

**Hashes:**
- produto `6186D377…`;
- validação `776DA64D…`;
- Preview `CED7570C…`.

**Rollback:**
- a Preview do N8.4 (`CE4A02EB…`) em `dist\BlueStinger-Preview.anterior`;
- a do N8.3 em `.n8-3-5d8bd6fe`;
- a do N8.2 em `.n8-2-cc370d56`;
- a do N8.1 em `.n8-1-9ecdeb7`.
