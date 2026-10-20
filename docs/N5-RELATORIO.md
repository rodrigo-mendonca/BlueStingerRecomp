# N5 (vídeos nativos): relatório de encerramento

Estado: **concluído em 2026-10-20 por testes dirigidos** (política de testes de 2026-10-20, nível 2). A
regressão integral foi **ADIADA POR DECISÃO DO PROJETO**, não aprovada (seção 7).

Planos e mapas: `docs/N5-PLANO.md`, `docs/N5-INTEGRACAO.md` e `docs/MODS-VIDEO.md`.
Commits: `af6b9c8` (integração e E001); o fechamento: o commit que traz este relatório.

## 1. Arquitetura entregue

- **A função de vídeo do jogo (`8C08B544`) continua a original.** Só as 8 entradas MW que ela chama
  são atendidas nativamente (`transitional/video_adapter`, slots em `[native] slots`). Handles de
  outros players vão ao original, e a entrada do pool é zerada (o servidor por handle `8C1588D8` a
  ignora).
- Nenhum Sofdec durante o filme nativo: 0 chamadas aos originais do MW e 0 leituras do SFD.
- **Builder:** SFD → MP4 (pl_mpeg + ADX → H.264/AAC pelo Media Foundation), manifesto dos 17 IDs,
  reaproveitamento por SHA-256, `--sem-filmes`, filmes ausentes sem codificador.
- **Jogo:**
  - catálogo com mods por ID ou nome;
  - relógio lógico (o mesmo resultado no headless e na janela, nos dois modos de tempo);
  - quadro do filme no lugar do quadro do PVR;
  - áudio pelo XAudio2 (saída nula no headless);
  - legendas SRT/WebVTT por idioma.
- **Interfaces para outras plataformas** (`docs/DECISAO-VULKAN-LINUX.md`, seção 3):
  - `IMovieDecoder`, com o Media Foundation como backend;
  - `ISubtitleRasterizer`, com o DirectWrite como backend;
  - `IAudioOutput`, com o XAudio2 como backend;
  - cor do sprite em `video/movie_color` (portável).

## 2. Os 16 filmes (`tools/dev/n5_matriz.py`, `analysis/n5/matriz.md`, sem reconverter)

| Item | Resultado |
|---|---|
| Reprodução pelo `MoviePlayback` (relógio lógico a 60 Hz, parâmetros do jogo) | 16 ok, OPENING ausente (não existe no disco US), 0 falhas |
| Integridade | SHA-256 de todos os MP4 igual ao manifesto; quadros tocados = quadros do SFD |
| Sincronia (fins que governam o fim no jogo) | 0 ms nos 16; instante do fim conferido contra a regra |
| Atraso do AAC | 0 amostras (o AAC do Windows não acrescenta priming); preenchimento final de 1–23 ms ignorado pelos fins da fonte |
| Imagem | PSNR médio de 40,2 a 48,4 dB, SSIM médio > 0,98; PSNR mínimo < 40 dB em 8 filmes (38,3–39,97 dB, quadros isolados), dentro da D4 (objetivo, não bloqueio) |
| Áudio | SNR de 23 a 47 dB contra o ADX (perda do AAC a 192 kbit/s; o mais baixo é o E185B) |

## 3. No jogo: E001 (`tools/dev/n5_e001.ps1`)

| Item | Resultado |
|---|---|
| Abertura pelo manifesto (ID 3) | ok |
| Sincronia | fim natural em 13 713 ms lógicos, 411 quadros, 0 pulados |
| Quadro na tela | 412 trocas em 828 VBlanks |
| Sem Sofdec | 0 chamadas aos originais; 0 leituras do `E001.SFD` (o legado lê 599) |
| Retorno ao gameplay | 1 070 passos com quadros e estado iguais ao legado, mesmas salas |
| Com a cor do O1b ligada | `n5_e001` **10/10** no fechamento (2026-10-20), com o modelo de cor ligado no `GameHost`; V8 (fim natural com START) e V9 (padrão = nativo) |

## 4. Oráculo O1b (Flycast, `analysis/n5/o1b`)

| Item | Resultado |
|---|---|
| NEWGAME (`ctl=00`) | entrada no passo 1032; cor base 0xC0 e offset 0, sem mudanças; estados 1 → 2 → 4; retorno 0 depois de 4 107 iterações. O Flycast encerra com erro por volta de 68 s de um filme de 452 s (hipótese: streaming do GD-ROM no Flycast); o nativo toca inteiro. Divergência documentada, não copiada |
| Cor (NEWGAME, `tools/dev/n5_cor.py`) | `framebuffer ≈ 0,65 × filme + 13` (correlação 0,99): **faixa de TV** do Sofdec × **cor base/255**. Modelo `0,753 × (16 + 0,859 × v)`: PSNR de 35,2 dB contra 22,3 dB da identidade |
| E001 (`ctl=01`): cor | **confirmada**: base 0xC0 e offset 0 na entrada, sem mudança durante o filme; capturas das iterações 100, 300 e 1000 com `k ≈ 0,66–0,675` e `c ≈ 12` (correlação 0,98–0,99); modelo com PSNR de 31,5/35,4/38,5 dB contra 17,0/21,2/26,9 dB da identidade; a iteração 30 é o preto da faixa de TV escurecido (média 12,3 = 16 × 0,753) |
| E001: estados, retorno, fade no fim | entrada na passada 6253 (`ctl=01`), estado 2 em 6261; fim em 8190 com estado 3 e retorno `r0 = 0` depois de 1 936 iterações (O1a, sem START). **Nenhum fade**: cor base e offset constantes até o fim |
| START durante o E001 (O1b-B) | START nas passadas 6672–6690 (UP mantido desde 2630, como a rota): o filme **não é encurtado**: fim em 8190, estado 3, `r0 = 0`, 1 937 iterações (1 936 sem START) |
| Preparação e regra de fim | a sondagem da palavra `0x8C24F878` como "pad" estava errada: ela nunca muda em nenhuma execução; a regra de fim é o fim natural do filme |

**Cor no nativo:** o modelo parametrizado (`MovieColorModel`, portável) é ligado pelo `GameHost` com
faixa de TV, cor base e offset aditivo, confirmado nos dois filmes observados. O núcleo isolado mantém
a identidade como padrão. O offset aditivo é a semântica da offset color do PVR; nenhum filme
observado mudou o offset.

## 5. START (`tools/dev/n5_start.ps1`)

O laço do filme sai por input lendo a palavra `0x8C24F878` (estrutura global `0x8C24D2CC` +
`0x25AC`):
- bit 4 ligado e bit 8 desligado, com `ctl & 1`;
- o caminho do bit 2 só vale para o ID 0.

O roteiro separa controle, estado do jogo, condição, parada pedida ao adaptador e retorno, nos modos
nativo e legado. Regra definitiva: a do O1b-B (o START **não** encerra o E001).

**Resultado: igual ao original.**
- O START caiu dentro da janela do filme: leitura 31 865, janela 31 446–32 273.
- A palavra `0x8C24F878` não mudou, a condição de saída não apareceu e o adaptador não foi parado.
- O fim do filme ficou no mesmo VBlank (32 293) da execução sem START, e o jogo voltou ao gameplay.
- **Mesma linha do tempo até o START:** os 31 884 registros de quadro (VBlank, hash do quadro, leituras e
  passo) da execução com START são idênticos aos da execução sem START. A conversão da rota do oráculo
  (passo → leitura) não deslocou nenhuma entrada (achado da revisão do fechamento, seção 8).
- Desde o fechamento, o roteiro **falha** (`exit 1`) se o comportamento divergir do O1b-B ou se a linha do
  tempo até o START diferir.
- **Nota sobre o NEWGAME:** o nativo toca os 452 s inteiros. O "parado pelo jogo em 86 786 ms" de uma
  observação anterior era só o fim da execução (encerramento do processo), não um efeito do jogo.

## 6. Builder, Preview, mods

| Item | Resultado |
|---|---|
| `test_builder` (caso dos filmes: 16 convertidos, 16 reaproveitados, `--sem-filmes` tira do pacote) | 22/22 |
| Preview com os 16 MP4 (417 MB) e manifesto dos 17 IDs | ok |
| Mods: substituição por ID/nome, outra resolução/FPS (1080p60), volta ao original, legendas por idioma, mod incompatível com diagnóstico, junção recusada, OPENING por mod | unitários (`bs_video_tests` 41/41) |

**Auditoria da distribuição (2026-10-20, leitura de `dist\`, sem reconverter):**

| Item | Situação |
|---|---|
| Executáveis do Builder | `BlueStinger-Recompiler.exe` (simples, pt-BR/en-US, modo portátil), `BlueStinger-Developer.exe` (projeto, log técnico), `bs-builder.exe`; textos por chave, idioma por ComboBox (`bs_ui_tests`) |
| Modo portátil | desligado por padrão: a Preview não tem `instalacao.toml`; o jogo o lê quando existe (`test_saves` S15–S18) |
| Saves e mods | Documentos ou pasta portátil; preservados na troca de instalação (`builder_install_swap_portable_config`, `test_builder`) |
| Filmes | 16 MP4 + `manifest.toml` com 17 IDs (OPENING `ausente`), conversor `n5-1 h264-q100 aac-192 bt601-tv` |
| Legendas | nenhum recurso no pacote: vêm de mods (`docs/MODS-VIDEO.md`) |
| Build offline | `kit\third_party` com SDL3 e toml++; o Builder passa `FETCHCONTENT_SOURCE_DIR_*` e `FETCHCONTENT_FULLY_DISCONNECTED=ON` |
| DLLs | runtime do MSVC local (`msvcp140*`, `vcruntime140*`, `concrt140`, `vccorlib140`); Media Foundation, XAudio2 e DirectWrite são do sistema |
| Licenças | Builder: projeto (MIT), SDL3 (zlib), toml++ (MIT), pl_mpeg (MIT). Preview: projeto, SDL3, toml++ |
| Sem CDI, BIOS, ROM ou dump | nenhum `.cdi/.gdi/.iso`, BIOS ou flash nos pacotes. A Preview leva `data\disc` (os arquivos do disco do próprio usuário, que o jogo lê; "uso local; não publicar" no `VERSAO.txt`) |
| Duplicação | a Preview leva os SFD (714 MB com o resto do disco) **e** os MP4 (417 MB): os SFD seguem necessários enquanto `--video legado` for o fallback |
| **Dependência externa** | **Build Tools do Visual Studio (MSVC)**, documentada no `LEIA-ME.txt` do Builder. Sem alternativa validada: o piloto LLVM-MinGW (worktree `toolchain-piloto`, `docs/PILOTO-LLVM-MINGW.md`) só cobre um programa de teste e o protótipo Vulkan; a decisão A/B (`docs/DISTRIBUICAO-COMPILADOR.md`) segue com o mantenedor |

## 7. Promoção (`--video nativo` padrão; `--video legado` fallback)

**Feita em 2026-10-20**, depois do O1b-B (START igual ao original, cor confirmada, nenhum fade, retorno ao
gameplay):
- `GameOptions::video` e `--video` com padrão `nativo`;
- `--video legado` continua como fallback temporário (até o N8);
- o `LEIA-ME.txt` da distribuição explica os filmes, a falta da pasta `data\video` e o fallback.

As suítes estabelecidas com o Sofdec ficam com `--video legado` no escopo histórico:
- `test_tempo`, `test_n2b`, `test_n2b2`, `test_cri`, `test_efeito`, `test_controles` (todos os casos);
- `test_gameplay`, `test_projeto`, `test_arquivos`, `test_ordem`, porque as rotas indexadas por leitura
  do controle quebram com o NEWGAME nativo de 7,5 min.

O padrão novo é provado pelo V9 do `n5_e001`.

**Regressão integral: ADIADA POR DECISÃO DO PROJETO** (mantenedor, 2026-10-20; política de testes em 3
níveis, `CLAUDE.md`). Não foi aprovada:
- **16 etapas aprovadas** no mesmo código do fechamento:
  - os builds, os unitários e o ctest com e sem disco;
  - `make_builder`, `make_preview`, `test_preview`, `test_gameplay`, `test_builder`;
  - `test_projeto`, `test_arquivos`, `test_ordem`, `test_tempo`;
- **`test_n2b` CANCELADA** depois de 13 min;
- **não executadas:** `test_cri`, `test_efeito`, `test_n2b2`, `test_assistido`, `test_controles`,
  `test_pd_nativo`, `test_saves` e a etapa `n5_e001` do executor;
- o estado fica em `%LOCALAPPDATA%\BlueStingerRecomp\regressao` (`regressao.log`, `cancelada.txt`). A
  integral volta no nível 3: antes e depois da remoção do legado no N8 e na revisão final do N9.

**Fechamento por testes dirigidos (nível 2, 2026-10-20), depois das correções da revisão:**
- builds e ctest **15/15**;
- `n5_e001` **10/10** (V8: fim natural com START, como no original; V9: o padrão é o nativo);
- `n5_start` com veredito **rc = 0** (1 438 s): igual ao original (O1b-B) no nativo, linha do tempo
  idêntica até o START (31 884 registros), legado sem erro. O legado não é referência de START: o Sofdec
  emulado encerra os filmes sozinho, e o E001 dele abre no VBlank 4 480.

**Depois das 16 etapas aprovadas, o código mudou só em:**
- correções de diagnóstico e ferramentas, cobertas pelos testes dirigidos (`BS_RAM_WATCH`, `n5_start`,
  `n5_e001`);
- duas suítes fixadas em `--video legado` (`test_controles`, N12 do `test_n2b2`), que voltam à configuração
  histórica e não foram executadas.

## 8. Revisões do Codex

Cinco rodadas no N5:
- `codex-20261019-160959`: 9 achados;
- `codex-20261019-164800`: 4 achados (2 ALTA resolvidos na integração);
- `codex-20261019-175202`: 2 achados;
- `codex-20261020-003154`: 3 achados nas ferramentas de validação (todos corrigidos);
- `codex-20261020-115542` (fechamento: só o diff não commitado): 5 achados, todos aceitos e corrigidos.

| Gravidade | Achado | Correção |
|---|---|---|
| MÉDIA | `BS_RAM_WATCH` mal configurado lançava exceção fora do `try` da thread do host (`std::terminate`) | inicialização dentro do `try` e limpeza por guarda RAII em todo retorno; unitário `game_host_ram_watch_bad_config_is_session_error` |
| MÉDIA | `n5_start.ps1` só imprimia os resultados (sem código de saída) | veredito pelo O1b-B nos dois modos, `exit 1` em qualquer divergência |
| MÉDIA | a conversão passo → leitura usa o log por VBlank, não a primeira leitura do passo | prova de efeito: os registros de quadro das execuções sem e com START são idênticos até o START (31 884 linhas); conferência permanente no roteiro |
| BAIXA | `test_controles` K1–K11 e o N12 do `test_n2b2` caíam no padrão nativo | `--video legado` em todos os casos (escopo histórico). O `est_antigo` do `test_n2b2` fica sem a opção, de propósito: o executável antigo não a conhece |
| BAIXA | nada provava que omitir `--video` seleciona o nativo | V9 no `n5_e001`: execução sem `--video` exige `[video] nativo:` |

Conferidos sem problema pelo Codex:
- a promoção nos dois níveis (`GameOptions` e `Args`);
- o modelo de cor (faixa de TV → base/255 → offset → saturação, alfa preservado), contra o disassembly
  `8C1761DA`/`8C1761F4` e o renderer do PVR;
- o `BS_RAM_WATCH` desligado por padrão;
- as suítes históricas restantes fixadas.

**Não verificado:** o arredondamento exato do PVR físico e o offset ≠ 0 numa captura real (nenhum filme
observado muda o offset).

## 9. Critérios

| Critério | Situação |
|---|---|
| Filme nativo no jogo sem Sofdec, com retorno ao gameplay | **concluído** (E001) |
| 16 filmes convertidos, íntegros e sincronizados | **concluído** |
| Mods de vídeo e legendas | **concluído** |
| Interfaces para outra plataforma | **concluído** |
| Fidelidade de cor (base e faixa) | **concluído** (NEWGAME e E001 no O1b-A); fade: nenhum observado |
| START igual ao original | **concluído**: O1b-B no Flycast; `n5_start` no nativo, com a mesma linha do tempo até o START |
| Promoção a padrão | **concluída** (`--video nativo`; legado como fallback até o N8) |
| Regressão integral | **ADIADA POR DECISÃO DO PROJETO** (seção 7), não aprovada; fechamento por testes dirigidos (nível 2) |
| Distribuição sem compilador do usuário | **bloqueada**, fora do N5 (`docs/DISTRIBUICAO-COMPILADOR.md`) |
