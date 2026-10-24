# Legendas em inglês por mod (falas e filmes)

Data: 2026-10-20. Worktree `n6-audio`, sem commit. Autorizado pelo mantenedor ("extração de falas e mod de legendas em
inglês").

O Blue Stinger original **não tem legendas**. Este trabalho transcreve as falas dos áudios originais (do disco do
usuário), gera legendas do zero e as entrega como **mod externo**: o jogo roda com as vozes originais em inglês e
legendas em inglês sem mudar o executável e sem recompilar. A transcrição é também a base de uma futura tradução para
PT-BR: o reconhecimento de voz não precisa ser refeito.

**O que está pronto e o que não está:**
- pronto: inventário, pipeline reproduzível, transcrição automática de todas as falas e filmes, o pacote no formato do
  jogo, os testes, a prévia para revisão; no jogo, as legendas ficam ativas pelos eventos das falas nativas;
- **não está pronto:** a revisão humana (0 linhas revisadas) e o **desenho das legendas das falas na tela** (N7; hoje o
  jogo só registra a legenda ativa no log);
- **filmes:** o player do N5 **já desenha** legendas de filmes na tela, do leiaute `mods\video\<NOME>.<idioma>.srt`; o
  pacote traz as mesmas legendas nesse leiaute (`video\<NOME>.en.srt`), então as legendas dos filmes já aparecem hoje.

## 1. Separação

| Parte | Onde | Git |
|---|---|---|
| Código do jogo: serviços de localização, eventos de diálogo, mods, sincronização | `native/localization`, `native/audio/voice_player`, `transitional/sound_adapter` (N6 fase E) | sim |
| Pipeline: catálogo, transcrição, geração do pacote, prévia, testes | `tools/dev/legendas/` | sim (só código e o glossário de nomes) |
| Trabalho: cópias PCM, transcrições, áudio extraído dos filmes, relatório, prévia, o pacote gerado | `%LOCALAPPDATA%\BlueStingerRecomp\legendas-en\` | **não** (dados derivados do jogo) |
| Ferramentas: venv do ASR e o modelo | `%LOCALAPPDATA%\BlueStingerRecomp\tools\asr`, `...\asr-modelos` | não |

O pacote só tem **texto**: nenhum áudio ou vídeo do jogo (verificado no teste `test_caminhos_seguros_e_sem_midia`).
Publicar o pacote ou as transcrições depende da decisão do mantenedor sobre a distribuição de texto derivado dos
diálogos.

## 2. Inventário (`tools/dev/legendas/catalogo.py`)

Ponto de partida: a tabela de nomes do player de falas (`0x8C157754`, N6 seção 16), conferida com o disco.

| Medida | Valor |
|---|---|
| IDs na tabela | 239 (0–238) |
| IDs vazios (o player ignora) | 6: 26, 87, 97, 98, 215, 217 |
| arquivos | 233, um a um com a tabela (nenhum faltando, nenhum sobrando) |
| formato | ADPCM da AICA (tag 0, 4 bits), 22 050 Hz; 232 mono, **1 estéreo (`E084.WAV`, ID 99)** |
| duração total | 1 421 s |

`catalogo.tsv` (na pasta de trabalho): ID, nome, existe, canais, taxa, bits, amostras, duração, SHA-256, categoria e a
**fonte** da categoria, contexto e a **fonte** do contexto, cópia PCM, situação da transcrição.

**Categoria** (sem inventar):
- pelo nome: 24 terminam em `ENV` (ambiente) e 209 são candidatos a fala;
- pela detecção de voz (passo 2), que vence o nome: **200 com fala, 32 sem fala**. O nome não é prova:
  - 3 `ENV` têm fala: `E017ENV`, `E024ENV`, `E028ENV`;
  - 11 sem `ENV` não têm fala detectada: `E006D`, `E033`, `E055D`, `E098D`, `E100`, `E022A`, `E045`, `E005`,
    `E089B_G`, `E089B_M`, `E127`;
  - as duas listas precisam de conferência de ouvido (gritos e grunhidos que não são palavras ficam sem legenda).
- **`E084` (ID 99) bloqueado:** ADPCM **estéreo**; a disposição dos canais no arquivo não foi medida (o player nativo do
  N6 também o recusa: pendente no N6). Não foi adivinhada.

**Contexto** (só o comprovado):
- **estático:** o chamador `8C02E0C0` sorteia um dos 16 IDs da tabela `0x8C0AAD20` (102, 188–201 e 2: `E089B_*`,
  `E001C`);
- **visto tocando** (oráculo do Flycast e o nativo na rota `passo_combate`, do NEW GAME ao combate): 221 `E012AENV` (na
  carga do NEW GAME), 222 `E012BENV` (início do gameplay, área 1), 4 `E002A` (depois do fim do `E012BENV`), 5 `E002B`,
  204 `E000ENV` (2 vezes);
- o resto: "desconhecido". Cena por ID e personagem por fala: **pendentes** (ver seção 7).

## 3. Metodologia

1. **Decodificação** (`catalogo.py`): cópia em Python do `native/audio/aica_adpcm.cpp`, provada igual ao jogo: o PCM
   decodificado de `E002A` e `E012BENV` tem a mesma impressão (FNV-1a 64) no Python e no `load_sound` do jogo (testes
   `test_igual_ao_jogo` e `audio_voice_wav_decode_matches_pipeline`).
2. **Reconhecimento** (`transcrever.py`): **faster-whisper 1.2.1** (MIT) com o modelo **large-v3** (MIT), local, na GPU
   (CUDA, float16; cai para CPU int8 se faltar), inglês, VAD (silêncio mínimo de 300 ms), `beam_size` 5, temperatura 0,
   sem condicionar no texto anterior, **tempo e probabilidade por palavra**. Todo o acervo (232 falas + 16 filmes) levou
   poucos minutos. Os filmes: o áudio é extraído do MP4 do Builder numa cópia temporária (`ffmpeg`, mono 16 kHz); o
   filme não é alterado.
3. **Geração** (`gerar_mod.py`), sem inventar texto:
   - **glossário** (`glossario.tsv`): só nomes com fonte no texto do jogo (`1ST_READ.BIN`): "Eliot G. Ballade", "ESER",
     "Kimra", "Kimratech"; cada troca fica registrada (28 linhas tiveram nomes corrigidos);
   - **confiança** da linha = média da probabilidade das palavras: `revisar` se alguma palavra < 0,5 ou média < 0,75;
     **excluída** (não vai para o jogo) se a média < 0,45, se for uma palavra só e duvidosa (vocalização), ou se tiver
     a **alucinação típica** do modelo em sons que não são fala (letra repetida ≥ 6 vezes, como um rosnado
     "GRRRR…", palavra maior que a linha, ou o índice de repetição do modelo > 2,4);
   - **quebra:** até 2 linhas de 42 caracteres; frases longas divididas nas palavras (de preferência na pontuação) com
     o tempo das próprias palavras; duração mínima de 0,8 s e 0,2 s de folga, sem invadir a linha seguinte nem passar
     do fim da fala (o fim arredonda para baixo: o teste pegou um fim 1 ms além da fala no `E076`);
   - `revisoes.tsv` (opcional, na pasta de trabalho): correções humanas por voz e linha, que vencem o automático.
4. **Prévia** (`previa.py`): MP4 de tela preta com o áudio da cópia PCM e a legenda como faixa, mais o SRT ao lado,
   para revisar no VLC/mpv (fora do jogo e fora do Git).

**Limite conhecido:** uma linha com confiança alta ainda pode estar errada. Exemplo: no NEWGAME, uma linha com o nome do
protagonista transcrito errado (confiança 0,81) não foi marcada. Por isso **nenhuma linha automática conta como revisada**.

## 4. Formatos (os contratos do jogo, `native/localization`)

```
mods/
  legendas-en/
    mod.toml                       formato = 1, id = "legendas-en", nome, versao, prioridade = 10,
                                   idiomas = ["en-US"], jogo = "T13001N V1.000"
    legendas/
      falas.en-US.toml             [[fala]] voz = "voz/e002a", inicio, fim (s, desde o inicio da fala), texto
      NEWGAME.en-US.srt            filmes: legendas/<FILME>.<idioma>.srt (o nome original do filme)
      E185A.en-US.srt ...
    fontes/
      transcricoes.en-US.tsv       cada linha: recurso, id, arquivo, linha, tempos, texto, texto do ASR,
                                   confianca, estado (automatica|revisar|revisada|excluida), motivo, glossario
      glossario.tsv
    video/
      NEWGAME.en.srt ...           as mesmas legendas dos filmes no leiaute do N5 (copiar para mods\video\):
                                   o player de filmes as desenha hoje (até a unificação das pastas de mods)
    README.md
```
Diferença do esboço do pedido: os filmes ficam em `legendas/<FILME>.<idioma>.srt` (o caminho que o `SubtitleService` já
procura), não em `legendas/filmes/`; e não há pasta `fontes/` de tipografia (a fonte do desenho é do N7). Idioma
`en-US` com recuo para `en`. Os IDs são os estáveis em minúsculas (`voz/<nome>`). Uma fala pode ter várias linhas
(cada uma com o seu tempo); a mesma voz pode ter linhas diferentes por cena (`cena`), quando a cena for comprovada.

## 5. Sincronização

- **Falas:** tempo relativo ao **início da fala** (o evento `Started` do player nativo: o momento em que o jogo vê
  "tocando"), nunca o tempo da sessão. A legenda acompanha o áudio: fim natural e interrupção a encerram; a pausa
  congela; a troca de cena encerra; a mesma fala repetida recomeça sem duplicar. Numa dublagem mais longa a legenda
  segue o áudio (o fim lógico do jogo não a encerra). Falas simultâneas: o jogo toca uma por vez (o player ignora o
  pedido com uma fala ativa, N6 seção 16).
- **Filmes:** o tempo de mídia do player do N5 (100 ns).

## 6. Cobertura (2026-10-20)

| Medida | Valor |
|---|---|
| arquivos catalogados | 233 (+ 6 IDs vazios) |
| com fala | 200 |
| sem fala | 32 |
| bloqueados | 1 (`E084`, estéreo) |
| linhas de legenda das falas | 685: **602 automáticas**, **79 a revisar**, 4 excluídas |
| linhas revisadas por humano | **0** |
| filmes processados | 16; **8 com fala** (NEWGAME, E017, E179, E180, E185A, E185B, E193, E195) com 45 linhas (34 automáticas, 11 a revisar); E001, E016, E107, E134, E200A, E200B, ENDING e ENDING2 sem fala detectada |
| **início do jogo** (`E002A`, `E002B`, `E003`, `E004A`, `E004B`) | 48 linhas: 43 automáticas, 5 a revisar; prévia gerada |
| cobertura por cena | só as falas vistas tocando têm cena (seção 2) |

## 7. Pendências

1. **Revisão humana**, começando pelo início do jogo (a prévia e as linhas `revisar` em `fontes/transcricoes.en-US.tsv`;
   as correções vão para `revisoes.tsv`).
2. **Cena e personagem por fala:** quais IDs cada chamador (`8C02E0C0`, `8C035E5A`) usa em cada cena, por oráculo de
   gameplay; personagem só com evidência (nunca pela voz).
3. **`E084` estéreo** (N6): medir a disposição dos canais (o canal da AICA no key-on, como os efeitos).
4. **Conferir de ouvido** as duas listas da seção 2 (nome × detecção).
5. **Desenho** das legendas das falas (N7) e o observador dos filmes do N5 no `SubtitleService` (N7, `docs/LOCALIZACAO-PLANO.md`
   seção 5).
6. **Tradução PT-BR:** partir de `fontes/transcricoes.en-US.tsv` (mesmos IDs e tempos); fora deste escopo.

## 8. Testes

| Teste | O que prova | Resultado |
|---|---|---|
| `tools/dev/legendas/test_legendas.py` (unittest) | decodificador igual ao do jogo (FNV, disco real); glossário só em palavra inteira; quebra em 2 × 42; confiança, exclusão e tempos; alucinação excluída; **o pacote gerado**: manifesto, IDs válidos e presentes no catálogo, UTF-8, tempos dentro da fala, ordenados e sem sobreposição, linhas ≤ 42, SRT válido e crescente, caminhos seguros, sem áudio/vídeo | **11/11** |
| `audio_voice_wav_decode_matches_pipeline` (C++) | o `load_sound` do jogo dá o mesmo PCM que o pipeline | ok (`bs_audio_tests` 41/41) |
| `loc_mod_legendas_en_gerado` (C++, `BS_LEGENDAS`) | o pacote gerado carrega no parser do jogo **sem problemas**; a fala `E002A` (começando aos 37,5 s da sessão) mostra as linhas do pacote nos tempos certos; interrupção; filme NEWGAME com legenda no tempo de mídia | ok (`bs_localization_tests` 8/8) |
| contratos já existentes | pausa, fim natural, troca de cena, repetição, ausência de legenda, IDs inválidos, caminhos de mods | `loc_subtitles_dialogue_events`, `loc_dialogue_bridge_voice_player`, `loc_mod_packages_priority_and_safety` |
| no jogo (`--audio-falas nativo --legendas en-US`, pacote em `mods\`) | rota de combate: 11 trocas de legenda no log, `E002A` linha a linha (VBlank 7194, 7386, …) e `E002B` | ok |
| `video_legendas_en_geradas` (C++, `BS_LEGENDAS`) | as cópias no leiaute do N5 são achadas pelo catálogo de filmes (`NEWGAME` → `en`) e lidas pelo parser do N5, em ordem | ok (`bs_video_tests` 42/42) |

Reprodução completa (da pasta `recomp-BStinger-n6`):
```
python tools/dev/legendas/catalogo.py <1ST_READ.BIN> <data\disc\BSDATA>
%LOCALAPPDATA%\BlueStingerRecomp\tools\asr\Scripts\python.exe tools/dev/legendas/transcrever.py
%LOCALAPPDATA%\BlueStingerRecomp\tools\asr\Scripts\python.exe tools/dev/legendas/transcrever.py --filmes <data\video>
python tools/dev/legendas/gerar_mod.py
python tools/dev/legendas/previa.py
python -m unittest discover -s tools/dev/legendas -p test_legendas.py   (BS_GAME_DATA, BS_LEGENDAS)
```

## 14. Inglês por padrão (2026-10-21, sem commit)

Pedido do mantenedor: legendas em inglês (en-US) ligadas por padrão nos filmes e nas falas, desligáveis e com outros
idiomas no futuro, sem duplicar o que o N5 e o N6 já têm.

- **Escolha do idioma:** `--legendas <idioma>|nenhuma` > `[legendas] idioma` do `instalacao.toml` (ao lado do
  executável; o Recompiler grava a seção junto com o modo dos saves e a regravação preserva a escolha) > **`en-US`**.
  `nenhuma` (ou `off`) desliga. Valor inválido: aviso no log e o padrão. Código: `native/platform/install_config`
  (`parse_subtitles`, `kDefaultSubtitles`), `app/main_sdl.cpp` (`subtitlesLanguage`).
- **Filmes (N5, sem mudança no player):** o mesmo renderizador de SRT/WebVTT; `en-US` acha `<NOME>.en-US.srt` e recua
  para `<NOME>.en.srt` (`modsideo`).
- **Falas (N6):** o mesmo `SubtitleService` + `DialogueBridge` (legenda presa aos eventos da voz que toca, original ou
  dublagem de mod com outra duração); só com `--audio-falas nativo` (o legado não gera eventos). O desenho no gameplay
  continua no **N7** (hoje: o log `[legenda] VBlank ...`).
- **Sem legendas no idioma:** nada aparece; o jogo segue como o original. O executável não traz legendas.
- **Provisórias:** o manifesto ganhou `provisorio = true` (conteúdo não revisado); `gerar_mod.py` o grava enquanto
  houver linha não revisada e o jogo avisa no log (`[legenda] pacote legendas-en 0.1.0: provisorio (nao revisado)`). O
  pacote gerado por IA continua fora do Git, do `dist/`, do kit e do Builder (fica em `%LOCALAPPDATA%`); o Builder só
  copia o `modsideo` do próprio projeto do usuário.

**Testes (2026-10-21):** `ui_install_config_subtitles` (padrão, nenhuma, pt-BR, inválido, gravação e releitura com os
saves) e `loc_mod_provisional_and_en_us_default` (manifesto provisório; `en-US` acha o pacote `en`); `bs_ui_tests`
25/25, `bs_localization_tests` 9/9, `bs_audio_tests` 69/69. No jogo (rota `passo_combate`, pacote ligado numa pasta
`mods` do build de teste, não no `dist/`): sem opção, o filme NEWGAME mostra "What's up, Belliot?" aos 143,2 s; com
`--legendas nenhuma`, o mesmo quadro sem texto; estado do jogo por passo idêntico com e sem legendas; o aviso de
provisório aparece.

**Pendências:** revisão humana (ex.: "Belliot" no NEWGAME é erro de transcrição); desenho das falas no gameplay (N7);
escolha do idioma na janela do Recompiler (hoje: o arquivo e a linha de comando); as duas formas de pastas de mods dos
filmes (risco 5).
