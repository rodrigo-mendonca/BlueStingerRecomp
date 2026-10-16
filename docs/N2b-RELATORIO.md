# N2b-0 / N2b-1: referências, diagnósticos e modo sombra (relatório)

Data: 2026-10-16. Plano: `docs/N2b-PLANO.md`. Base: N2a (`fdf326a`).

Autorização do mantenedor: **só N2b-0 e N2b-1**.
- D7: relógio lógico como alvo, condicionado ao N2b-0.
- D8: erro com limite comprovado.
- D9: Maple e GD-ROM no próximo ponto de espera, depois de comprovado; render medido antes.
- D4: interface só de progresso e esperas.
- D10: dependências por JSON ligadas.

**N2b-2 não autorizado. Sem commit.**

## 1. Resumo

| Pergunta | Resposta (com evidência) |
|---|---|
| A compilação voltou? | **Sim.** Release, Debug e mods compilam do zero com dependências por JSON (D10). Defeito do lançador no Debug (`/Fd<pasta>\`) corrigido; o teste incremental agora reproduz a falha antiga (C1041) e passa com a correção |
| O modo sombra muda a execução? | **Não.** Trace de ordem completo (com ciclos) igual ao legado byte a byte; trace de 3 000 VBlanks = referência do N2a (`176F4C28…`), com e sem o monitor ligado |
| Esperas declaradas | 3, no gerador (`BS_WAIT`/`BS_WAIT_END`). Em 8 000 VBlanks: VBlank do jogo 3 792 episódios, todos liberados por VBlank-In; render 17 episódios, todos por fim de render; nenhuma saída sem evento |
| Ociosidade do servidor do CRI (thread 3) | **Parcial.** "Nenhuma das 32 entradas ativa" foi **descartada** (muda as leituras de arquivo). "Sem entrada MW, sem ADX e sem stream em uso" não muda nenhum artefato lógico, e as diferenças restantes (áudio no vídeo, SPG) se reproduzem num controle só de tempo. Mas a condição só vale em boot e menus (27% das passadas): no gameplay o stream de ambiente fica sempre aberto (seção 4) |
| Render | Custo estimado de 0,14 a 0,45 campo; nenhum termina depois do VBlank-In seguinte; a espera de render só bloqueou 17 vezes em 3 986 renders |
| D8 (limite de progresso) | Calibrado: **64 000 000** pontos de controle. Maior janela legítima: 17 232 307 (cargas); quadros normais < 1 000 000 |
| N2b-2 pode começar? | **Não** (seção 8) |

## 2. Compilação (prioridade 1)

- **Lançador `tools/build/msvc_srcdeps.cmake`:** no build Debug, o CMake passa `/Fd<pasta>\ /FS`
  a executáveis. Dentro do script, montar a lista de argumentos com `list(APPEND)` faz a barra
  final escapar o separador `;`, e dois argumentos viravam um. Resultado: C1041.
- **Correção:** a barra invertida final vira `/` (o `cl` aceita); argumento com `;` é erro claro.
- **O teste incremental não pegava:** o projeto mínimo era uma biblioteca, e para biblioteca o
  `/Fd` recebe um nome de arquivo. Virou executável, compilado em Debug no modo JSON.
  - Conferido: com o lançador antigo o teste **falha** (C1041); com o novo, passa.
- Build do zero de Release (82 s), Debug e mods (82 s) com `BS_MSVC_DEPS_JSON=ON`.
- **Unitários:**
  - um teste do Builder travava a suíte: o kit falso não tinha `tools/build`, agora exigido pelo
    `check_kit`. Corrigido;
  - o executor de testes agora despeja a saída a cada teste, para um travamento futuro mostrar
    qual foi.
- **Builder e Preview** compilados do zero (cache do Builder renomeado):
  - `test_preview` 9/9; `test_builder` 18/18;
  - a Preview distribuída tem o código do N2b-1 (aceita `BS_TEMPO_MUTACAO=sem_espera`).
- **Backups:**
  - `dist/BlueStinger-Preview.n2a-fdf326a` (executável do N2a);
  - `.n1-770e3d1`, `.anterior`, `.legado-cdi` intactos;
  - caches antigos do Builder em `build.antes-n2a`, `build.n2a-1`, `build.n2a-2`.

## 3. Modo sombra (N2b-1)

**O que foi implementado (só observação):**
- `native/platform/time.h`: `IProgress` (D4: sem métodos de troca de thread);
- `native/platform/progress.*`: `ProgressMonitor`;
- gerador: `[native] esperas` no config, com `BS_WAIT` no back-edge e `BS_WAIT_END` na
  instrução seguinte, por onde passam todas as saídas;
- `Sh4Context::polls`: pontos de controle executados, contados no `BS_POLL`; é trabalho
  estrutural, não tempo;
- ganchos `progress`/`wait_point`/`wait_end` no provedor de tempo (vazios no legado);
- trace `BS_PROGRESS_TRACE`.

**Medidas (8 000 VBlanks, NEW GAME):**

| Thread | Trabalho por VBlank (pontos) | Observação |
|---|---|---|
| 0 (principal) | mediana 562; p99 ~830 mil | inclui as voltas da espera do VBlank |
| 1, 2 (tarefas do jogo) | 4 | um passo por VBlank |
| 3 (servidor do CRI) | mediana 113 312, máximo 117 640 | ~300–340 passadas do servidor por VBlank, o tempo todo |

**Decisões do modelo** (se o tempo pudesse andar): 1,79 bilhão de voltas de espera com
"avançar", 1 024 com "continuar" (evento vencido), 0 deadlocks.

**Janelas de trabalho sem espera** (calibração do D8, duas rotas): cerca de 3 790 por rota.

| Janela | Ocorrências | Onde |
|---|---|---|
| ≥ 100 mil e < 1 milhão | 3 791–3 793 | quadros normais |
| 2 a 17 milhões | 5 | cargas: boot V34 (16,6 M), após o vídeo V2254 (17,0 M), 2ª sala V4630 (17,2 M), V3163 (3,6 M), V2265, V4640 |

Limite = 64 M (≈ 3,7 × a maior). Só é válido para a cobertura atual; a campanha completa pode
ter cargas maiores (risco, seção 7).

## 4. Ociosidade do servidor do CRI (thread 3)

**Estrutura** (disassembly e traces):
- Laço da thread 3 (`8C158840`): se `[0x8C1DD1C0] == 1`, cede a vez com `8C17DDCC` (troca
  explícita para a thread em `[0x8C2D1D20]`, pedida por `8C1588AA`); senão roda a passada
  `8C1586B8`.
- A passada chama, cada um medido pelo TMU e com interrupções mascaradas
  (`8C157F94`/`8C157FAC`):
  - `8C178874` (servidor ADX);
  - `8C15863A` para cada uma das 32 entradas ativas (`0x8C2D0D84`, 120 bytes);
  - `8C094E14` (chamada de sistema do GD-ROM, `EXEC_SERVER`) duas vezes;
  - `8C15BFD2` (estado do drive do gdFs);
  - `8C175976`, que despacha para o objeto sempre registrado `0x8C1DD5E0`, método `8C174888`.
- `8C174888` → `8C174BAA` → `8C190E0C` → `8C1C7628` → `8C1D92EA` → gdFs `ReqRd32`: é o
  streaming de som (`E012AENV.WAV` aberto por `8C1D923A`).

**Medidas (2 400 VBlanks):**
- 96,6% das passadas têm 0 entradas ativas; a única ativa é a entrada 0, só durante o vídeo;
- os handles ADX ficam ativos exatamente quando a entrada 0 fica;
- o objeto servidor está registrado em 100% das passadas.

**Experimento** (`BS_CRI_DIAG`, só no build com mods, nunca no produto): pular as passadas com
0 entradas ativas, em 8 000 VBlanks.

| Artefato | Resultado |
|---|---|
| Ordem lógica (sem PC), estado do jogo, quadro | iguais |
| Leituras de arquivo | **diferentes**: `E012AENV.WAV` 1 VBlank depois, 8 blocos a mais |
| Escritas de áudio na AICA | **diferentes** a partir do vídeo (V2097) |

**Conclusão: "nenhuma entrada ativa" não é ociosidade.**
- O streaming de som roda em toda passada.
- A condição "sem entrada, sem ADX e sem objeto servidor" nunca é verdadeira, porque o objeto
  está sempre registrado; o experimento com ela não pulou nada.
- Também: `8C094E14` e `8C15BFD2` consultam o GD-ROM e o drive (HLE legado) a cada passada.

**Condição refinada: "sem stream"** (engenharia reversa de `8C174888`)
- O gerenciador de streaming tem controle em `0x8C2DA958`: `+0` = quantidade (16), `+4` = base
  `0x8C2D84E0`. Entradas de 68 bytes: `+0` ≠ 0 = em uso, `+48` = pedido pendente, `+60` = handle.
  Fontes: `8C174998`, o predicado `8C1749E0` e `8C174A36`.
- **Condição:** nenhuma entrada MW ativa, nenhum handle ADX ativo e nenhum stream em uso.
- Vale em **717 383 de 2 666 236 passadas (27%)**: boot e menus, até o vídeo abrir o primeiro
  stream (`NEWGAME.SFD`, VBlank ~2094).
- Depois disso, o stream de ambiente (`E012AENV.WAV`) fica aberto o tempo todo, e a condição
  nunca mais é verdadeira no gameplay.

**Experimento e controle** (8 000 VBlanks, mesmo roteiro):

| Artefato | `pular3` (pula as passadas "sem stream") | `repetir3` (controle: executa 2× as mesmas passadas; só o tempo muda) |
|---|---|---|
| TN (ordem lógica), CL (leituras), SL (estado), quadro | **iguais** | iguais |
| SPG_STATUS (valor) | difere (V230) | difere (V40) |
| Escritas na AICA | difere a partir de V2097 (vídeo) | difere a partir de V2097 |
| Ordem com PC interrompido | difere | difere |

**Leitura:**
- Pular as passadas "sem stream" não muda nenhum artefato lógico.
- As diferenças de áudio e de SPG aparecem também no controle, que não omite trabalho nenhum.
  Elas vêm do deslocamento de tempo (os ciclos estimados mudam), não de trabalho perdido.
- As escritas na AICA durante o vídeo dependem do tempo de execução (quantas passadas de ADX
  cabem antes da preempção). Isso é um fato do legado, não do experimento.
- Pulando, a thread 3 faz 349 M passadas em vez de 2,7 M: girar no vazio é barato. No N2b-2, a
  thread teria de **bloquear** nessa condição, não pular.

**Estado: condição comprovada só para boot e menus**, nesta rota. Para o gameplay falta o estado
"stream em uso sem pedido pendente" (`+48`, `8C1749E0`), ainda não provado. É o critério que
bloqueia o N2b-2.

## 5. Render e temporizador

- **Render** (3 986 renders):
  - custo estimado 469 200 a 1 500 000 ciclos (teto da estimativa), ou 0,14 a 0,45 campo;
  - **nenhum termina depois do VBlank-In seguinte**;
  - o fim é entregue em até 2 011 ticks (poll);
  - a espera de render só bloqueou em 17 episódios (o render costuma já ter terminado).
  - Para a D9: um fim de render "no próximo ponto de espera" não mudaria o passo de lógica neste
    roteiro, mas os 17 episódios mostram que a espera existe. A decisão fica para depois do S4.
- **Temporizador:** consumidores na seção 1.4 do plano. Com o TMU parado por quadro (`quadro`
  e `agendado`, que usa o instante devido do VBlank), o critério B11 compara ordem lógica,
  leituras e quadro (seção 6).

## 6. Testes

**Regressão completa (2026-10-16, em sequência, Preview recompilada pelo Builder):**

| Suíte | Resultado |
|---|---|
| Unitários | 210/210 (inclui `n2b_progress_repeat_wait_equivalent` e `n2b_progress_expected_event`) |
| `ctest` (sem e com a imagem) | 12/12 (só `boot_trace` pulado) |
| `make_builder`, `make_preview` | ok |
| `test_preview` | 9/9 |
| `test_gameplay` | 4/4 (7 300 VBlanks sem CDI) |
| `test_builder` | 18/18 |
| `test_projeto` (mods) | 11/11 |
| `test_arquivos` | 6/6 |
| `test_ordem` | 8/8 |
| `test_tempo` | 35/35 (depois da correção abaixo) |
| `test_n2b` | **44/44** |

**`test_tempo`: defeito meu no artefato, não no comportamento.**
- Na primeira rodada, o A2 acusou 3 986 entregas "antecipadas".
- Eram os registros diagnósticos `RenderStart` do N2b-0 (devido = fim agendado, registrado no
  STARTRENDER), lidos como entregas.
- O A2 passou a ignorá-los. Conferido nos traces gravados: 0 antecipadas, atraso máximo de 2 120
  ticks (o mesmo do N2a).

**`test_n2b`** (`tools/dist/test_n2b.ps1`): cada negativo tem de ser detectado pelo comparador do
próprio critério, e um artefato vazio nunca conta como diferença.

| Critério | Oráculo (legado × sombra, 8 000 VBlanks) | Negativo detectado |
|---|---|---|
| S1 | trace de ordem completo igual byte a byte (duas rotas de arquivos) | — |
| S2 | toda saída com o evento esperado entregue no episódio (VBlank 3 792/3 792; render 17/17, maior episódio 1,49 M ticks) | render perdido: saída sem o evento após 54 M ticks |
| S3 | 0 faltas com o limite de 64 M (duas rotas) | espera ignorada → falta de progresso |
| S4 | chamadas a `8C1586B8` e `8C174888` por VBlank, quantidade e ordem (3 376 VBlanks, 1 000 742 passadas) | `menos1`: uma passada a menos por VBlank |
| B1 | TN | VBlank-In extra |
| B2 | IRQ por VBlank e por campo lógico | IRQ ausente; IRQ extra |
| B3 | CL | bloco corrompido |
| B3b | CK (SPG só a quantidade) | relógio em degraus |
| B4 | fundidos 14 = legado nos dois provedores; 0 colapsos | fusão → IRQ por campo diferente + colapsos |
| B5 | maior episódio < estouro | render perdido → estouro (≥ 0,9 × 166 ms) |
| B6 | SL, quadro, escritas na AICA | variável alterada na RAM |
| B7 | 0 deadlocks; 0 faltas | espera ignorada; fila vazia → deadlock (unitário) |
| B8 | duas execuções iguais (TN, CL, CK, SL, quadro) | — |
| B9 | 16,95 ms ≤ 16,80 × 1,05 | `lento` (19,00 ms) |
| B11 (parcial) | TMU parado por quadro (`quadro`, `agendado`): TN, CL e quadro iguais | — |

**Desvios do plano, registrados:**
- **B4:**
  - O plano pede TN igual entre `--arquivos legado` e `nativo`. Hoje ela difere pelo tempo de
    carga (já conhecido no N1), e a diferença fica como informação; o contrato P1 continua no
    `test_arquivos`.
  - O plano pede 0 fundidos, mas o legado tem 14 no boot; o critério é "igual ao legado".
- **B7:** o caso "laço sem espera → guarda com PC" informa a thread, não o PC.
- **B10:** coberto só pelas suítes; falta "mod de função de espera sem a primitiva dá erro".
- **B11:** faltam os valores dos medidores do CRI e o consumidor artificial que decide pelo TMU.

**Negativos que precisaram de ajuste, e por quê:**
- **Mutações de VBlank a cada 1 000:** com 3 000 VBlanks, o efeito sumia na sequência por
  VBlank. Passaram a valer a cada 97, e o artefato de IRQ por campo lógico foi acrescentado.
- **Leitura corrompida na 2 000ª:** o roteiro curto não chegava a ela. Passou para a 20ª.
- **Deriva de tempo:** não muda a ordem lógica, por construção. O negativo do B1 virou um
  evento a mais.

## 7. Riscos

- **Thread 3 sem condição de ociosidade comprovada:** bloqueia o N2b-2.
- **Limite D8** calibrado só em duas rotas de 8 000 VBlanks.
- **`SPG_STATUS`** devolve a posição do feixe, que depende do tempo de execução: o critério de
  leituras compara só a quantidade dessas leituras (o experimento do CRI mostrou o valor
  mudando).
- **VBlanks fundidos** existem em operação normal (14, todos no boot) e dependem do tempo.
- **Caminhos legados por passada:** o servidor do CRI chama o GD-ROM HLE (`EXEC_SERVER`) e o
  estado do drive (`8C15BFD2`) centenas de vezes por VBlank.

## 8. Quando será seguro iniciar o N2b-2

O que ainda bloqueia:

1. **Ociosidade da thread 3 no gameplay** (condição obrigatória do mantenedor).
   - Provado: "sem MW, sem ADX e sem stream em uso" não tem efeito lógico, e as diferenças
     restantes se reproduzem num controle só de tempo.
   - Mas a condição só vale em boot e menus. No gameplay, o stream de ambiente fica aberto.
   - Falta provar a condição "stream em uso sem pedido pendente" (`+48`, predicado `8C1749E0`)
     com o mesmo par experimento + controle, em rotas de gameplay mais longas.
   - Sem isso, a thread 3 no N2b-2 não teria onde bloquear, e não se pode substituir a
     preempção por uma espera fictícia.
2. **Áudio dependente do tempo de execução.**
   - As escritas na AICA durante o vídeo mudam com o deslocamento de tempo (controle
     `repetir3`).
   - Com o tempo vindo do relógio lógico, o volume de trabalho do ADX por campo vai mudar.
   - Precisa de um oráculo de áudio que aceite isso (quantidade de amostras entregues, não a
     sequência exata de escritas), decidido antes do N2b-2.
3. **B10 e B11 incompletos:** o mod de função de espera, os valores dos medidores do CRI e o
   consumidor artificial do TMU.
4. **Limite D8** calibrado em só duas rotas de 8 000 VBlanks: precisa de rotas mais longas
   (outras salas, cargas de save).
5. **B4 entre provedores de arquivos:** a TN difere pelo tempo de carga. É preciso definir se o
   N2b-2 iguala isso (GD-ROM no ponto de espera, D9) ou se o critério continua sendo o P1.
6. **"Liberada por"** prova a entrega do evento esperado no episódio, não a transição da
   condição no jogo. Para o N2b-2, observar a transição (leitura da bandeira em `8C064A28` e
   `8C17307C`) seria mais forte.

O modo sombra está pronto para aprovação: a regressão completa passa, e a execução não muda.
