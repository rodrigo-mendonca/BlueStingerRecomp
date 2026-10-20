# N5: vídeos nativos (plano)

Autorização (2026-10-19): iniciar o N5 em paralelo à regressão do N4, primeiro com os trabalhos
independentes (inventário, fronteira, arquitetura, revisão do Codex, componentes isolados). Sem commit,
sem N6. Base: `docs/PLANO-PORT-NATIVO.md` (N5 e diretriz 4.1), contrato do N0
`native/video/video.h` e decisão de 2026-10-15: vídeos convertidos pelo Builder e tocados com Media
Foundation, desde que formato e fidelidade sejam validados.

## 1. Escopo e critérios de aceitação

**Escopo:** a função de vídeo do jogo (`8C08B544`): **17 IDs, 16 recursos presentes e uma ausência
contratual** (ID 0, `OPENING.SFD`, não está no disco US).

**Fora do escopo:**
- o áudio de eventos (`E*.WAV`, ADX): ciclo `8C08B860`/`8C08B8B8`/`8C08B900`/`8C08B980` com
  `PlyCreateWav` (`8C158FC4`), inicializado junto com o Sofdec (`8C158E98`). É fronteira do **N6**;
- música e efeitos;
- renderização (N7).

**Critérios:**

| # | Critério | Como se prova |
|---|---|---|
| V1 | Nenhuma chamada Sofdec durante os filmes no modo nativo (create, start, estado, stop, destroy e o alocador de 3 MiB). O `mwPlyInitSofdec` do boot continua: ele inicializa a biblioteca que o WAV do N6 ainda usa. Não se afirma ociosidade da thread 3, porque o stream de ambiente fica aberto | contador de entradas MW = 0 entre o início e o fim de cada filme |
| V2 | Contrato do slot igual ao original, pela **tabela de decisão** da seção 2.3 (bits 0/1/2 de `r5`, flags da entrada, estados 3/4, START, interrupções 2/4/8, fade até `0xFF`) e pelo caso ausente (ID 0) | oráculo (Flycast) e legado por cenário (combinações, não só por ID); unitários do slot com lixo em `r6`/`r7` |
| V3 | Fidelidade no modo fiel: quadros contra **duas** decodificações independentes do MPEG-1 (`pl_mpeg` no Builder e ffmpeg, só no desenvolvimento) e contra capturas do Sofdec no Flycast, alinhadas por timestamp. Matriz BT.601 em faixa de TV, PSNR e erro máximo por canal com limites fixados (D4). Áudio: PCM do ADX igual a uma referência independente (feito: idêntico ao ffmpeg), mesmo número de amostras, e a perda e o atraso de priming do AAC medidos e compensados | `test_video` V3; probe |
| V4 | Sincronia: a posição vem de um relógio lógico injetado, amostrado uma vez por iteração depois da espera do guest. Áudio com política definida (D2) | determinismo por VBlank nos dois modos de tempo; execução mais rápida que o tempo real, atraso proposital, pausa |
| V5 | Mods: por ID, sem recompilar; mais curto, mais longo, mesma duração em outra resolução, outra taxa e taxa variável; manifesto inválido; legendas | `test_video` V5 e unitários do catálogo |
| V6 | Erros (ausente, corrompido, sem decodificador) com diagnóstico claro; para filme ausente, o comportamento do original | unitários e `test_video` |
| V7 | Builder converte offline. O executável e o projeto exportado funcionam com o CDI de origem removido ou renomeado | `test_builder`, `test_projeto` |
| V8 | `--video legado` selecionável e exercitado até a promoção | caso explícito em `test_video` e suítes existentes |

## 2. Inventário (2026-10-19, bs-disasm, Ghidra, banco do N0, probe; revisado pelo Codex)

### 2.1 Arquivos

Conferidos pelo `bs-video-probe`: 16 `.SFD` em `BSDATA` (~470 MB), Sofdec (MPEG-PS, "Sofd").
- Vídeo MPEG-1 em `0xE0`, **320x240 a 30 fps**.
- Áudio **ADX** em `0xC0`: estéreo, 44,1 kHz, versão 3, corte de 500 Hz.
- Em todos, quadros ÷ 30 = amostras ADX ÷ 44 100. Por exemplo, E001 tem 411 quadros = 13,70 s =
  604 170 amostras; NEWGAME tem 13 583 quadros = 452,77 s; ENDING tem 11 773 quadros = 392,43 s.
- O fim pelo **último timestamp de vídeo** difere do fim do áudio em alguns campos NTSC; o Codex
  mediu E200B e ENDING2.
- O **decodificador ADX** do Builder é **idêntico bit a bit ao do ffmpeg** em E017, E200A e NEWGAME
  (44 milhões de amostras). A correção necessária: coeficientes arredondados, não truncados.
- Há amostras saturadas também na referência: é do conteúdo.

### 2.2 Biblioteca CRI MW (Sofdec 1.04), banco do N0

| Endereço | Função |
|---|---|
| `8C157FC4` | `mwPlyCreateSofdec` |
| `8C158B70` | `mwPlyInitSofdec` (boot, `8C07893C`) |
| `8C15814A` | `mwPlyStartFname` |
| `8C158250` | `mwSfdStop` |
| `8C1582C0` | `mwSfdGetStat` |

Fora a create e a init, o acesso é indireto, pela tabela do objeto (`+0x14` destroy, `+0x18` start,
`+0x1C` stop, `+0x20` estado). A análise estática só acha `8C08B544` chamando a create.

### 2.3 Função de vídeo do jogo: `int movie(int id /*r4*/, int8_t ctl /*r5*/)` em `8C08B544`

**ABI.** São **dois** argumentos: `r4` é o ID e `r5` o controle (`exts.b r5` em `8C08B55E`). Os três
chamadores (`8C0786F8`, `8C07878E`, `8C0787AA`) preparam só `r4` e `r5`, que vêm de `[+52]` e
`[+54]`. A assinatura de quatro argumentos do Ghidra estava errada.

**Tabela** `8C157688`, 12 bytes por ID: `+0` = `0xC0` (parâmetro do player), `+4` = flags,
`+8` = nome. Só o ID 0 tem flag 1; os demais têm 0.

| ID | Arquivo | ID | Arquivo |
|---|---|---|---|
| 0 | `OPENING.SFD` (ausente) | 9 | `E193.SFD` |
| 1 | `NEWGAME.SFD` | 10 | `E195.SFD` |
| 2 | `E017.SFD` | 11 | `E200A.SFD` |
| 3 | `E001.SFD` | 12 | `E200B.SFD` |
| 4 | `E107.SFD` | 13 | `ENDING.SFD` |
| 5 | `E179.SFD` | 14 | `E016.SFD` |
| 6 | `E180.SFD` | 15 | `E134.SFD` |
| 7 | `E185A.SFD` | 16 | `ENDING2.SFD` |
| 8 | `E185B.SFD` | | |

**O que o corpo faz:**
- **Alocador:** `8C15C81A` aloca 3 MiB e devolve bloco+32; `8C15C8D6` libera. É do player MW e
  sai no caminho nativo.
- **Fade de brilho:** `8C08B520` mais `8C158D92`/`8C158DAE` → `8C1761DA`/`8C1761F4`, com o byte
  replicado em RGB e alfa `0xFF`, de 0 a 255 e volta. O slot o **reproduz** sobre o quadro.
- **"PRESS START BUTTON":** `8C08AE80` desenha glifos; `8C1714DC` salva e restaura o estado gráfico
  em `0x8C2D9CE0`. Só aparece com a flag de tabela bit 1, que nenhum ID tem. Se um mod ligar, o slot
  desenha o texto **nativamente**, porque o PVR não fica por cima de um quadro que não passa por ele.
- **Chamadas do guest que permanecem:** `8C064A52`, a espera `8C1745E6` e, quando o laço continua,
  `8C174472`.
- **Áudio:** não há pausa explícita da música ou dos efeitos no corpo; o áudio do filme é interno
  ao Sofdec. A leitura anterior ("som") estava errada.

**Tabela de decisão** (a fixar e testar no O1; o fluxo foi lido pelo Codex):

| Condição | Efeito |
|---|---|
| `r5 & 1` | START pode encerrar (`8C08B600`–`60A`) |
| `r5 & 4` | fim ou START saem já; sem o bit, esperam a transição (`8C08B60C`–`612`) |
| `r5 & 2` | estado inicial da transição e o valor `0xFFFFFFFF` (`8C08B56A`–`578`) |
| interrupção externa bit 2 | retorna 1, ou 2 com a flag de tabela bit 0 |
| interrupção bits 4 ligado e 8 desligado | retorna 2 (`8C08B730`–`754`) |
| estado MW 3/4 ou START | encerra o laço conforme as flags (`8C08B7A0`–`7C8`) |
| flag de tabela bit 0 | converte o normal em 1 e o START em 3; sem ela, fica 0 (`8C08B7F2`–`820`) |

**Regra de fim:** o original termina quando o Sofdec passa ao estado 3/4, e não por uma duração
calculada. O O1 fixa o instante (último quadro de vídeo, fim do áudio ou o que o Sofdec usar) e o
manifesto guarda os fins de cada stream e a duração contratual.

## 3. Fronteira

Um **slot nativo em `8C08B544`** reproduz a função inteira pela tabela de decisão:
- as mesmas chamadas do guest que não são MW (seção 2.3);
- o fade e, se for o caso, o texto, desenhados nativamente sobre o quadro;
- os mesmos códigos;
- no lugar do player MW, o player nativo.

Os quadros vão para o apresentador do host, que já aceita qualquer tamanho
(`app/main_sdl.cpp`).

- **Por que aqui:** é um ponto só, com contrato pequeno e observável. O jogo não lê quadros do
  Sofdec, até onde foi verificado; o Codex marca isso como não provado para toda a campanha. Mods de
  outra duração só mudam o instante do fim.
- **Alternativa rejeitada:** reimplementar o objeto player MW. Seria um Sofdec nativo, com o
  desenho preso ao PVR.

### 3.1 Fronteira revisada (2026-10-19, depois da leitura completa de `8C08B544`)

Portar à mão as ~380 instruções da função (a tabela de decisão) é arriscado. A fronteira passa a ser
a mesma do N3 (`pd`) e do N4 (`bu`): **a função de vídeo do jogo continua a do original**, e um
provedor nativo atende **as entradas MW que ela chama**. Os retornos, o START, as interrupções e as
flags ficam iguais ao original **por construção** (V2), e nenhuma função Sofdec roda (V1).

| Entrada | Endereço | Papel observado |
|---|---|---|
| create | `8C157FC4` | pool estático de 32 objetos de 120 B em `[8C1580B8]+4`; `+0` = tabela `8C1DD168` |
| parâmetro | `8C158D42(h, 1)` | parâmetro 3 do player |
| cor base | `8C158D92(h, v)` | RGB(v,v,v) com alfa `0xFF` no sprite do filme (`+92`, `8C1761DA`); a tabela passa `0xC0` |
| cor de offset | `8C158DAE(h, v)` | idem em `+96` (`8C1761F4`): o fade de `8C08B520` |
| start | método `+0x18` = `8C15814A` | |
| estado | método `+0x20` = `8C1582C0` | 1 → 2 (tocando) → 3/4 (fim) |
| stop | método `+0x1C` = `8C158250` | |
| destroy | método `+0x14` = `8C1580E4` | |

- O objeto do player vem do mesmo pool estático do MW: os métodos são chamados pelo jogo através da
  tabela de 8C1DD168, cujos alvos são os slots.
- O alocador de 3 MiB (`8C15C81A`/`8C15C8D6`) é do SDK, não do Sofdec: fica como está.
- A matemática de cor (cor base `0xC0` e offset do fade aplicadas ao quadro) é fixada por capturas
  do quadro no Flycast (O1b) e reproduzida pelo apresentador nativo.

## 4. Arquitetura

### 4.1 Builder (offline, a partir do CDI do usuário)

1. **Demux** do SFD, próprio. **Feito.**
2. **ADX**, próprio. **Feito**, idêntico ao ffmpeg.
3. **MPEG-1:** `pl_mpeg` (D1).
4. **MP4 por Media Foundation:** H.264 e AAC, sem MFT de hardware, para o resultado não depender da
   GPU; um quadro de saída por quadro original.
5. **Manifesto** `data\video\manifest.toml`: ID, nome original, arquivo, quadros, fps, dimensões,
   fins de cada stream, duração contratual, atraso do AAC, SHA-256 do MP4 e impressão digital dos
   quadros de referência.

### 4.2 Runtime: `native/video`

- **`MfMovieReader`/`MfMovieWriter`:** **feitos**, com unitários sintéticos.
- **`MovieCatalog`:** **feito**. Builder, mods por nome ou manifesto e legendas por idioma.
- **Relógio:** uma interface injetável de ticks racionais, sem `Sh4Context`. O slot a amostra uma
  vez por iteração, depois da espera do guest, e grava `movie_start` depois do início. No headless
  é a posição lógica; na janela também, porque o ritmo do host já segue 59,94 Hz.
  O `video.h` do N0 ("relógio do próprio vídeo") passa a dizer isso.
- **Áudio (D2):** o vídeo é o mestre (relógio lógico). Uma fila XAudio2 com um teto de latência.
  - silêncio em underrun;
  - se a posição saltar ou atrasar mais que o teto, descarta e reinicia;
  - na pausa do host, pausa a voz;
  - na perda do dispositivo, troca para saída nula com log.

  Não antecipa o N6: é só a saída de um stream PCM.

### 4.3 Transitório: `transitional/video_adapter`

Slot `8C08B544` em `[native] slots`, com `--video nativo|legado` (padrão legado até os portões).

## 5. Testes

- **Unitários (feitos):**
  - demux de PS sintético;
  - ADX: bloco à mão, ida e volta e erros;
  - catálogo: Builder, mods, legendas, manifesto quebrado;
  - MP4 sintético por MF e erros.
- **Unitários a fazer:**
  - demux e decodificação dos 16 SFD reais (pelo probe, com o disco);
  - vetor ADX de referência externo;
  - slot: tabela de decisão completa com lixo em `r6`/`r7`, fade e ausente;
  - relógio: mais rápido que o tempo real, atraso, pausa;
  - legendas.
- **Oráculo O1** (Flycast), por ID e por combinação de `r5` e interrupções: código, quadros, VBlanks
  até o estado 3/4, ID 0 ausente. Também capturas de quadros do Sofdec para o V3.
- **`test_video.ps1`:**
  - V1 a V6, com os mods curto, longo, mesma duração, outra taxa e taxa variável;
  - V8, o legado explícito;
  - o V7 fica nas suítes do Builder e do projeto, com o CDI renomeado.

## 6. Subetapas

| Etapa | O quê | Estado |
|---|---|---|
| N5-0 | Inventário, fronteira, plano, Codex (`reviews/codex-20261019-145826.md`, 9 achados aceitos), O1 | O1 pendente |
| N5-1 | Demux, ADX, MP4 por MF, catálogo; unitários sintéticos | **feito** (worktree), falta a referência MPEG-1 (D1) |
| N5-2 | Relógio injetável, player com política de áudio, legendas; unitários | depois de D2/D3 |
| N5-3 | Builder (conversão, manifesto, kit) | depois do N4 e de D1/D4 |
| N5-4 | Slot e tabela de decisão, apresentação, `--video`; `test_video`; Codex; promoção | depois do N4 |

## 7. Decisões que dependem do mantenedor

| # | Decisão | Recomendação |
|---|---|---|
| D1 | Referência MPEG-1 no Builder | `pl_mpeg` (MIT), fixada e incluída no kit; ffmpeg só como segunda referência no desenvolvimento |
| D2 | Áudio dos filmes | XAudio2 mínimo, com o vídeo (relógio lógico) como mestre e a política da seção 4.2 |
| D3 | Legendas | SRT e WebVTT por idioma, desenhadas por DirectWrite; o jogo original não tem legendas (recurso só de mods) |
| D4 | Qualidade e limites | H.264 de software com bitrate alto; PSNR ≥ 40 dB por quadro e erro máximo por canal a fixar com os dados; AAC de 256 kbit/s com o priming compensado; BT.601 em faixa de TV |
| D5 | Definição de V1 | "nenhuma chamada Sofdec durante os filmes" (a init do boot fica para o N6) |

### 7.1 Decisões aprovadas (mantenedor, 2026-10-19)

- **D1:** `pl_mpeg` como referência MPEG-1 no Builder, com versão fixa, licença MIT incluída,
  testes de equivalência e compilação offline. Os termos e a versão incorporada ficam registrados.
- **D2:** saída mínima XAudio2 para o áudio dos filmes, atrás de uma interface que o N6
  reaproveita, sem antecipar o áudio do gameplay.
- **D3:** SRT e WebVTT externos por idioma, desenhados por DirectWrite. São opcionais,
  independentes do vídeo e substituíveis por mods, inclusive para novas traduções.
- **D4:** MP4/H.264 de bitrate alto e AAC de 256 kbit/s, em BT.601 adequado à fonte.
  - PSNR e SSIM medidos, com **PSNR ≥ 40 dB por quadro como objetivo, não como bloqueio**, quando a
    diferença for justificada.
  - A referência original fica preservada; atraso e sincronia documentados.
  - Os mods **não se limitam** a H.264 nem a 320x240/30 fps: aceitam qualquer resolução, taxa e
    formato que o Media Foundation decodifique, com diagnóstico claro quando não houver suporte.
- **D5:** no modo nativo, nenhuma função Sofdec é necessária durante a reprodução. A inicialização
  compartilhada com o áudio de eventos fica até o N6, instrumentada e documentada.

## 8. Isolamento durante a regressão do N4

- O código do N5 é escrito numa **git worktree** (`../recomp-BStinger-n5`), a partir de `b1c76ec`
  (sem as mudanças do N4 em validação), com build próprio e só dos alvos `native` e unitários.
- Nada nos arquivos que a regressão usa: `app/`, `transitional/`, `config/`, `tools/dist/` e os
  executáveis em `build/`.
- A integração (N5-3 e N5-4) só começa depois do fechamento do N4.

## 9. Mapa de integração (preparado em 2026-10-19, para depois do fechamento do N4)

A worktree não tem nenhum arquivo em comum com as mudanças da árvore principal ainda não commitadas.
Os arquivos novos entram por cópia, e os `CMakeLists.txt` (native, builder, tests e transitional) por
mescla de poucas linhas.

**O que só acontece na integração (arquivos compartilhados):**

| Arquivo | Mudança |
|---|---|
| `config/bluestinger_us.toml` | `[native] slots` mais as 8 entradas MW; gerar o código de novo (`bs-recomp`) |
| `app/game_host.{h,cpp}` | `VideoProvider` (`--video nativo|legado`), instalação do `video_adapter`, quadro do filme no lugar do quadro do PVR enquanto `playing()`, resumo no fim |
| `app/main_sdl.cpp` | `--video`, `--legendas <idioma>`, saída XAudio2 na janela e nula no headless |
| `builder/builder_core.cpp` | passo `build_movies` (data\disc\BSDATA → data\video) depois dos dados de execução |
| `make_builder.ps1`, `kKitEntries`, `project_export` | `third_party/pl_mpeg` e `builder/video` no kit (compilação offline) |
| `tests/CMakeLists.txt` | `bs_video_tests` no `bs_unit_tests` (ou mantido à parte) |
| `tools/dist/test_video.ps1` | nova suíte (V1–V8), e a etapa na `regressao.ps1` |
| `LEIA-ME.txt`, `CLAUDE.md`, `STATUS.md` | uso, mods de vídeo e legendas, evidências |

**Já pronto na worktree** (unitários `bs_video_tests` 19/19): demux, ADX, conversão e fidelidade,
`build_movies` e manifesto, catálogo com mods, legendas e caminhos seguros, leitura MP4 com conversão
BT.601 própria, reprodução pelo relógio lógico, saídas XAudio2 e nula, legendas por DirectWrite,
núcleo MW e a cola com o guest (`video_adapter`, ainda não compilada: depende do runtime).

**Pendente do O1b:** a matemática de cor (base `0xC0` e offset), o número de consultas em
preparação, a regra de fim e o estado do filme ausente.

## 10. Resultados do oráculo O1 (Flycast, jogo original; `analysis/n5/o1/o1a.tsv`)

| Filme | ID | `ctl` | Estados | Retorno | Passos do oráculo |
|---|---|---|---|---|---|
| NEWGAME | 1 | `00` (sem START) | 1 → 2 → **4** | 0 | 4 106 |
| E001 (chegada à área 1, passo 6252) | 3 | `01` (START permitido, não apertado) | 1 → 2 → **3** | 0 | 1 936 (13,7 s de filme) |

- **Semântica do estado 4** (estático): o MW só grava 4 no campo de estado (`+8`) em
  `8C158676`, `8C158954`, `8C1597B6` e `8C159824`. Em todos, isso acontece logo depois de uma
  consulta ao estado de erro de um handle subordinado (`+56`, `+88`, `+16`, por
  `8C1586B4`/`8C1589EC`/`8C1598F0`) devolver diferente de zero. O 3 é gravado em `8C158630` e
  `8C159D76` (fim normal).
  - **Comprovado:** 4 é erro de um componente do player (stream ou decodificador), não um fim normal.
  - **Hipótese, não verificada:** o NEWGAME falha no streaming do GD-ROM no Flycast e no legado
    (que também "termina cedo"); no hardware o filme tocaria até o fim.
  - **No nativo,** o filme toca até o fim (estado 3), e o retorno ao jogo é o mesmo (0). É uma
    divergência documentada em relação ao Flycast.
- O "passo" do oráculo não é um quadro (o E001 deu ~141 passos por segundo de filme). O O1b mede em
  iterações do laço do filme (`8C157F60`).
- O O1b, já preparado (`n5_oraculo.py --capturas`), mede iterações até 3/4, as mudanças de cor
  base e offset e captura o framebuffer, para a matemática de cor. Roda com o Flycast livre e sem a
  regressão do N4.

## 11. Revisão do Codex do código da worktree (`reviews/codex-20261019-160959.md`, na worktree): 9 achados, todos aceitos

| Gravidade | Achado | Correção | Teste |
|---|---|---|---|
| ALTA | a 2ª execução do Builder falhava (`rename` não substitui no Windows) | `MoveFileExW(REPLACE_EXISTING, WRITE_THROUGH)` | `video_build_twice_replaces_manifest` |
| ALTA | `flush` do XAudio2 podia liberar blocos ainda lidos | destrói e recria a voz (síncrono); tocados preservados | `video_audio_xaudio2_output` |
| MÉDIA | o priming do AAC não era removido | descartado do início do PCM (`audioSkipFrames`) | `video_playback_priming_and_source_end` |
| MÉDIA | o fim usava os EOFs do MP4 (preenchimento do AAC) | fins da fonte do manifesto para recursos do Builder; EOFs para mods | idem |
| MÉDIA | COM desbalanceado e por thread | `ComScope` RAII por objeto (MF, XAudio2, DirectWrite/WIC) | os existentes |
| MÉDIA | junção ou link escapava da pasta de mods | contenção depois de `weakly_canonical` | `video_catalog_junction_escape` (junção real) |
| MÉDIA | suíte fora do ctest; cola do guest sem teste | `add_test(video)`; o teste do `video_adapter` com memória sintética fica para a integração (depende do runtime) | parcial |
| BAIXA | `stop` não voltava a 0 depois de 3/4 | sempre 0 (`8C1582B2`) | `video_mw_core_stop_after_end_and_failing_output` |
| BAIXA | falha de `write` perdia áudio em silêncio | mesma política do dispositivo perdido | idem |

`bs_video_tests`: 23/23.

## 12. Adaptador testável, matriz e auditoria (2026-10-19)

**Adaptador.** A lógica da cola (`transitional/video_mw_glue`) não depende do runtime: fala com o
guest por uma `GuestPort` (ler e gravar memória, ler string, relógio lógico). As funções de slot
(`video_adapter.cpp`) só traduzem registradores. Testes de contrato com RAM sintética
(`test_video_adapter.cpp`):
- ciclo completo com o pool e a tabela gravados no guest;
- stop antes do start e destroy durante a reprodução;
- handles alheios para o original;
- três falhas de create (pool cheio, literal ilegível, segundo player);
- ausente e reinício;
- relógio parado e salto;
- falha de áudio e recuperação.

Eles validam o **contrato do adaptador**, não a equivalência com o Dreamcast. O START é decidido
pela função original do jogo, que fica intacta: o teste dele é o `test_video` com o jogo rodando.
`bs_video_tests`: 33/33.

**Matriz.** `tools/dev/n5_matriz.py` gera a matriz dos 17 IDs a partir do probe e do O1
(`analysis/n5/matriz.md`, fora do git). As colunas de conversão se preenchem com `--conversao` quando
a máquina estiver livre; as de reprodução, na integração.

**Auditoria do Builder, da Preview e da exportação:**

| Item | Situação |
|---|---|
| Conversão no Builder | `build_movies` a partir de `data\disc\BSDATA` (a cópia já conferida), sem o CDI de novo |
| Reaproveitamento | por SHA-256 da fonte, do conversor e do MP4; substituição atômica (`MoveFileExW`) |
| Distribuição | `data\video` vai junto com `data\` (a instalação copia a árvore inteira) |
| Preview sem CDI/SDK | só MP4 e manifesto; o runtime usa o Media Foundation e o XAudio2 do Windows |
| Projeto exportado | o kit já inclui `native/` e `transitional/`; as dependências novas vêm do Windows SDK (offline) |
| `pl_mpeg` | só no executável do Builder (`builder/video`), nunca no jogo nem no kit |
| Windows sem codificador H.264 (edições N) | **corrigido**: os filmes ficam "não convertidos" (o catálogo os lê como ausentes), a compilação segue e um mod pode fornecê-los |
| Git | nada de vídeo, `data\` ou `analysis\` versionado (`.gitignore`) |
| Lacunas para a integração | licença do `pl_mpeg` em `licencas\` da distribuição do Builder (`make_builder.ps1`); `bs_transitional` incluir `video_adapter.cpp` e ligar `bs_video_mw` |

## 13. Caminho curto até o primeiro vídeo nativo no jogo

1. **Depois do N4:** mesclar a worktree (seção 9), regenerar o código com as 8 entradas em
   `[native] slots` e ligar `--video nativo` no `GameHost`, com o quadro do filme no lugar do quadro
   do PVR enquanto `playing()`.
2. **Converter o E001** (13,7 s) com o `bs-video-probe --builder`, para `dist\...\data\video`.
3. **Rodar a rota até a área 1** (`passo_newgame_area1`): o E001 toca na chegada (O1: passo 6252) e
   deve devolver 0 depois do estado 3. Conferir V1 (nenhuma entrada MW executada), o retorno e a
   continuidade da rota.
4. **O1b com a máquina livre:** a cor base `0xC0` e o offset, para fechar o quadro final (hoje sem
   cor aplicada).

## 14. Segunda revisão do Codex (`reviews/codex-20261019-164800.md`, na worktree): 4 achados

| Gravidade | Achado | Decisão |
|---|---|---|
| ALTA | o `video_adapter` não entra no executável e o `GameHost` não o instala | **procede; é a integração** (arquivos compartilhados com o N4, seção 9). O teste do slot real com `Sh4Context` e `mods::call_original` entra junto |
| ALTA | o Builder não chama `build_movies` nem liga `bs_video_convert` | **procede; é a integração** (`builder_core.cpp`), com um teste do fluxo público do Builder |
| MÉDIA | o parâmetro (`8C158D42`) engolia chamadas de outros players (o original grava em `objeto+0x170+4*parametro`, `8C192164`) | **corrigido**: encaminhado; teste em `video_adapter_order_and_foreign` |
| BAIXA | chaves de idioma do manifesto sem normalização | **corrigido**: minúsculas; teste com `ES` no manifesto e pedido `es-MX` |

`bs_video_tests`: 33/33.
