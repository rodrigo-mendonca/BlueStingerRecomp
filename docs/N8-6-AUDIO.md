# N8.6: áudio e AICA legados

**Situação (2026-10-27): concluído**, pela autorização contínua do mantenedor até o fim do N8. Referência imutável do
N8.5: o commit `49a9a08`.

## 1. Inventário (somente leitura)

### 1.1 O que o jogo faz na área de som com o áudio nativo (o padrão desde o N6)

O som vem do mixer nativo: efeitos, falas e música passam pelos adapters do N6 (`transitional/sound_adapter`, slots do
SDK). O SDK de som recompilado continua a inicializar e consultar a área da AICA. Medido pela sonda (`BS_HW_PROBE`,
`newgame_area1`, 6 000 VBlanks):

| Quem | O quê |
|---|---|
| `8C08EE60` | limpa e preenche a RAM de som (cerca de 800 mil palavras, `0x00800000-0x009FFFFF`) e escreve o controle `0x00702C00` |
| `8C08EBD0` | lê a palavra de estado em `0x00810000` (5 972 vezes) e o RTC (`0x00710000`/`04`, 2 vezes por VBlank) |
| `8C172634` | uma escrita no controle `0x00702C00` |
| tabela do SDK (`8C08BD74`), `8C097094`, `8C0971C0` | registradores do G2 (`0x005F7800-0x005F78FF`). Os de partida do DMA (`0x5F7818/38/58`) recebem 0 |
| `8C09AA60` | lê o estado do DMA do G2 (`0x5F7818`, 143 832 vezes) |

**Nenhum registrador de canal, DSP, temporizador ou interrupção do ARM7** (`0x00700000-0x0071FFFF`, fora o controle e o
RTC) é tocado, e nenhum DMA do G2 parte.

### 1.2 O que ficava no produto

| Item | Uso |
|---|---|
| `runtime/aica/aica_regs` | modelo da área da AICA: RAM de som, controle e RTC, mais os diagnósticos `BS_AICA_TRACE`, `BS_AICA_LOG` e `BS_AICA_MUTACAO` |
| `--audio legado`, `--audio-efeitos/-falas/-musica legado`, `BS_AUDIO=legado` | o SDK de som e o driver Manatee originais escrevendo na AICA |

**Fica:**
- o código recompilado do SDK de som (AOT, até o N10): inicialização e consultas acima;
- o áudio nativo (`native/audio`: mixer, Manatee em C++, ADPCM, DSP, XAudio2) e os adapters do N6;
- o RTC, que é a hora do sistema e agora vive na área de som nativa.

## 2. Decisão

A opção recomendada, a mesma dos N8.3 a N8.5 (autorização contínua): um serviço nativo com o contrato medido e o modelo
só na validação. As alternativas foram descartadas:
- slots no SDK de som (`8C08EE60`, `8C08EBD0`): mudariam o código gerado e a RAM do guest (proibido: AOT intacto);
- manter o modelo no produto: a AICA continuaria alcançável.

## 3. Arquitetura

- **`system/sound_bus`** (papel `nativo`), sem AICA, ARM7 nem driver:
  - RAM de som de 2 MB (com o espelho até `0x00FFFFFF`);
  - controle `0x00702C00`;
  - RTC no relógio do provedor de tempo (liberação de escrita como o modelo);
  - a mesma instrumentação que as referências comparam (`BS_CLOCK_LOG` `AudRd`/`Aud`/`Rtc`, `BS_INTC_TRACE` `Rtc`);
  - qualquer outro registrador da AICA, ou outro tamanho, é erro explícito (contado em `sound_bus_refused`);
- **fachada `snd_bus_*`:** memória, boot e o diagnóstico do CRI usam o provedor ativo;
- **modelo da AICA** (`runtime/aica`): vai para o `bs_runtime_hle`. O `GameHost` da validação o instala com qualquer
  provedor de áudio legado ou com `BS_AICA_*`. O `bs-diff` usa sempre os modelos (Maple com o controle e AICA), o que
  também corrige o `bs-diff` depois do N8.4;
- **produto:** recusa (saída 2) o áudio legado, por qualquer das opções ou pelo `BS_AUDIO=legado`, e os `BS_AICA_*`;
- **DMA do G2 no produto:** a partida (bit 0 em `0x5F7818/38/58/78`) é erro explícito. Gravar 0 e ler o estado
  continuam;
- **`Audio*Provider::None`:** sem adaptador e com a área de som nativa, só para os jogos falsos dos testes.

## 4. Revisão independente (Codex, `codex-20261027-111725`)

| Achado | Decisão |
|---|---|
| [BAIXA] a recusa de `BS_AICA_*` não cobria `BS_AICA_DUMP_AT` e `BS_AICA_LOG_TUDO` | **aceito**: as cinco variáveis do modelo selecionam o modelo na validação e são recusadas no produto (pela presença), no `main_sdl` e no `GameHost` |
| [BAIXA] os testes não provavam a instrumentação nem toda a matriz de recusa | **aceito**: o `n86_sound_area_matches_aica_model` compara o arquivo do `BS_CLOCK_LOG` e o dump do `BS_INTC_TRACE` dos dois provedores; a catraca recusa `--audio legado`, `--audio-efeitos/-falas/-musica legado`, `BS_AUDIO=legado` e as cinco `BS_AICA_*` |

## 5. Equivalência e testes (2026-10-27)

**Exe do produto** (`FE89D6AB…`): 16/16 nas referências, com as 179 exceções de SPC e nenhuma nova.

| Teste | Resultado |
|---|---|
| unitários | 332/332 no produto, 331/331 na validação (o teste do DMA do G2 é só do produto) |
| ctest Debug | 19/19, com e sem o disco |
| catraca | produto 35, validação 43; recusa do áudio legado e das `BS_AICA_*` |
| `test_pd_nativo` | 86/86 |
| `test_saves` | 25/25 |
| `test_n8_nativo` | 26/26 |
| `test_n2b2` | 17/17 |
| `test_assistido` (validação) | 28/28 |
| `test_ordem` (validação) | 8/8 |
| bateria H do N6 (validação), H2, H3, H5 e H6 | aprovadas |
| H4 | 3 falhas **anteriores ao N8.6** (abaixo) |
| H8 (`-Build build\msvc-release`) | aprovada: controles 31/31, `n5_e001` 10/10, saves 25/25. Na bateria, a 1ª rodada procurou o `bs-video-probe` no `build
6` padrão do script, da época do N6 |
| `test_builder` | 22/22 |
| `test_projeto` | 18/18 |
| `test_preview` | 12/12 |
| `test_gameplay` | 4/4 |
| H9 | 0 faltas |

### 5.1 H4: falhas anteriores ao N8.6 (critério desatualizado desde o `e13a123`)

O H4 (tudo nativo × tudo legado, rotas LOAD e heliporto) reprova em 3 checagens:
- **estado por passo:** todos os passos comuns são iguais, mas o legado tem 4 480 passos e o nativo 4 475 nos 9 000
  VBlanks. O critério aceita no máximo 2. O nativo chega a cada passo 5 a 10 VBlanks **depois** do legado;
- **música (LOAD e heliporto):** os números estão certos (1 895 e 5 521 notas, 0 erros, 0 recusadas). A expressão
  regular exige que a linha termine em `recusadas 0`, e ela termina com o campo `espera do DMA`.

As duas mudanças vêm do `e13a123` (2026-10-23, N2 11.1). Desde ele, o download nativo da música espera o DMA (4
downloads, 180,3 ms), e o resumo ganhou o campo novo. O comentário do H4 ainda descreve o nativo 1 a 2 VBlanks antes. O
H4 passou pela última vez em 2026-10-21 e não tinha sido rodado depois.

**Prova:** o exe de validação do N8.5 (`49a9a08`, compilado numa worktree com o mesmo `generated/` e o `n6_h.ps1` do
próprio commit) dá as mesmas 3 falhas, o mesmo deslocamento (LOAD 5, 9 e 10 VBlanks; heliporto 5) e os mesmos números.
Os quatro traços de estado por passo (LOAD e heliporto, legado e nativo) são **idênticos byte a byte** entre o N8.5 e o
N8.6.

O critério do H4 **não foi alterado**. Ajustá-lo (o deslocamento de passos e a expressão da música) fica como pendência
para decisão do mantenedor.

**Tamanho:** o produto tem 19 231 744 bytes (N8.5: 19 244 032).

**Rollback:** a Preview do N8.5 (`CED7570C…`) em `dist\BlueStinger-Preview.n8-5-ced7570c`, além das anteriores.
