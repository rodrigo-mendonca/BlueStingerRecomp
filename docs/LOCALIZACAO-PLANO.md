# Localização: traduções, legendas inéditas e dublagem por mods (plano, 2026-10-20)

**Objetivo:** um jogador instala um mod PT-BR copiando arquivos para `mods\`, sem recompilar o `BlueStinger.exe`.
O mod pode trazer:
- tradução dos textos originais;
- legendas novas nos filmes e no gameplay;
- dublagem brasileira opcional;
- fontes próprias.

**Sem mods, o jogo se comporta como o original.**

Roadmap mantido: N5 concluído → **N6 Áudio** → N7 Vulkan → pendências do N2 → N8 → N9 (remasterização) → N10 (mods). Nesta etapa entram só
as fronteiras e os contratos difíceis de acrescentar depois. O overlay gráfico fica para o N7 e as ferramentas
de edição para o N10.

Legenda deste documento:
- **[feito]** já implementado e testado;
- **[preparado]** contrato ou infraestrutura pronta, sem ligação ao jogo;
- **[futuro]** etapa indicada;
- **[mod]** conteúdo que pertence aos pacotes, nunca ao executável.

## 1. Tradução de textos

O jogo tem textos (menus, itens, mensagens) que um mod deve poder trocar por ID estável, com recuo para o
original e preservando os códigos especiais do motor de texto.
- **[preparado]** `TextLocalization` (`native/localization/text_localization`): `textos/<idioma>.toml`. O idioma
  exato vence o recuo; no mesmo idioma, vence o pacote de maior prioridade. IDs e UTF-8 são conferidos. Uma
  tradução que muda os códigos especiais é recusada e fica o original.
- **[futuro, N10]** mapear os códigos do motor de texto do Blue Stinger e onde o jogo lê cada texto (a fronteira),
  e exportar os textos originais. Sem o mapa dos códigos, o contrato recebe o extrator (`Tokenizer`) pronto para
  ligar.

## 2. Legendas inéditas (requisito explícito)

**O Blue Stinger original não tem legendas.** Legendas são uma funcionalidade nova, só de mods, nos filmes e
também durante o gameplay.
- **[feito, N5]** legendas de filmes por mod: SRT/WebVTT por idioma (`--legendas <idioma>|nenhuma`, padrão en-US desde 2026-10-21, `mods\video`),
  desenhadas pelo DirectWrite no quadro do filme (`native/video/subtitles`, `MovieCatalog`).
- **[preparado]** `SubtitleService` (`native/localization/subtitle_service`):
  - uma só linha do tempo para filmes (tempo de **mídia**) e falas (tempo **lógico**), por eventos;
  - não desenha e não mexe na lógica do jogo.
- **[futuro, N7]** desenho sobre o gameplay e sobre o filme pelo `ISubtitlePresenter`.

## 3. Dublagem por mods

- **[feito, N6]** `SoundCatalog` (`native/audio/sound_catalog`) já é o núcleo da **VoiceLocalization**:
  - resolve `voz/<NOME>` por idioma (`pt-BR` → `pt` → neutro → original), em WAV, FLAC e OGG;
  - sincronia `original` (o jogo vê a duração original; padrão das vozes) ou `arquivo`;
  - `logical_end_frames` e o `LogicalEnd` do mixer;
  - teste `audio_dub_duration_policy`. **Não foi duplicado.**
- **[feito, N6 fase E; jogo pendente]** a voz do jogo liga ao catálogo pelo provedor nativo das falas
  (`--audio-falas nativo`, `--dublagem <idioma>`): `mods\audio\<idioma>\voz\<nome>.(wav|flac)`. O fim que o
  jogo vê é sempre o da fala original: a dublagem mais longa avisa o fim lógico ali e continua soando
  (`TailPolicy::PlayOut`, padrão; `Cut` corta no stop do jogo); a mais curta é completada com silêncio até o fim
  original. Testes `audio_voice_*` e `loc_dialogue_bridge_voice_player`.
- **[feito]** FLAC: `dr_flac` (domínio público/MIT-0, `third_party/dr_libs`, commit fixo), atrás de
  `parse_flac`; o resto do áudio não conhece a biblioteca. **OGG/Vorbis: pendente** (o `stb_vorbis` preferido
  tem CVEs de 2023 sem correção publicada; decisão do mantenedor, `docs/N6-PLANO.md` seção 16).

## 4. Componentes e contratos (separados; compartilham IDs, idiomas e pacotes)

| Componente | Responsabilidade | Onde | Estado |
|---|---|---|---|
| idiomas e IDs | BCP-47 (`valid_language`, `canonical_language`, `language_fallback`), ID estável (`valid_resource_id`), UTF-8 | `localization/locale` | [preparado] |
| pacotes | `mods\<pacote>\mod.toml`, ordem determinística, caminhos seguros, compatibilidade, recurso ausente | `localization/mod_packages` | [preparado] |
| TextLocalization | textos que **já existem** no jogo | `localization/text_localization` | [preparado] |
| SubtitleService | legendas **novas** (filmes e falas), linha do tempo por eventos | `localization/subtitle_service` | [preparado] |
| VoiceLocalization | vozes substituídas por idioma, duração lógica | `audio/sound_catalog`, `audio/voice_player` | [feito]; no jogo pela fase E (falta a prova no jogo) |
| ponte das falas | eventos do `VoicePlayer` → `SubtitleService` no tempo lógico do mixer | `localization/dialogue_bridge` | [feito] (teste); ligada no `GameHost` com `--legendas` |
| apresentação | `ISubtitlePresenter`, `SubtitleStyle`, `layout_subtitle`, `ITextMeasurer` | `localization/subtitle_presenter` | [preparado]; desenho no N7 |

## 5. Eventos de voz e vídeo

O jogo **não** emite esses eventos; quem os emite é a **fronteira nativa**, no instante em que o provedor
nativo atende a chamada original. O timing não muda, porque o evento é efeito colateral da própria chamada.

| Evento | Fronteira que o emite | Tempo | Estado |
|---|---|---|---|
| filme começou / tempo de mídia / acabou / pulado | adaptador MW do N5 (`transitional/video_adapter`: create/start/stat/stop) e `MoviePlayback` (relógio lógico 60 Hz, quadros, fim pela regra) | mídia (100 ns) | o player já tem os estados; **falta só o contrato de saída** (abaixo) |
| fala começou (`Started`: a consulta em que o jogo vê "tocando") / fim lógico (`LogicalEnd`: estado 3) / fim do áudio (`PhysicalEnd`) / interrompida (`Interrupted`: stop antes do fim, ou cauda cortada) / ausente (`Missing`) | provedor nativo das falas (`transitional/sound_adapter` + `voice_mw_glue` sobre o MW WAV: create, start, estado, stop, destroy) | lógico | [feito] no código e nos testes; **no jogo: pendente** da primeira fala nativa |
| pausa / retomada | `VoicePlayer::pause` (pausa do host). O jogo não pausa a fala pelo MW (o método +0x28 não tem chamador) | lógico | [feito] no player; a origem no jogo é **PENDENTE** |
| troca de cena | carga de área/evento; `DialogueBridge::set_scene` e `VoicePlayer::scene_changed` | lógico | contrato pronto; a fonte da cena no jogo é **PENDENTE** |

**Alteração mínima necessária no N5** (não feita aqui; o N5 está consolidado em `25ba624`): um observador opcional
no `MwCore`/`MoviePlayback` com `started(nome)`, `media_time(t)`, `ended()` e `skipped()`, chamado onde o estado do
filme já muda. Hoje as legendas do filme são desenhadas **dentro** do player (DirectWrite). No N7 o observador
alimenta o `SubtitleService` e o desenho vai para o apresentador comum. Etapa: N7, junto do apresentador.

## 6. IDs e contexto

O que o N6 já provou no binário (seções 10–12 do `docs/N6-PLANO.md`), por família:

| Família | Caminho no jogo | ID estável | É fala? |
|---|---|---|---|
| **Efeitos** | fila `8C02A6B8(id, a = volume, b = pan)` → 7 canais → SDK (`8C15E088(h, id/128, id%128)`) → `SOSB#banco`, descritor `pedido` | `efeito/<banco>/<pedido>` (211: 128 + 83) | **não** |
| **Falas** | só **2 pontos** chamam o player WAV `8C08B860`: `8C02E0C0` e `8C035E5A`. Os dois passam um **número de 16 bits lido de uma tabela** (`0x8C0AAD20`, passo 4; `0x8C0AB268`, passo 2). O player converte pela tabela de nomes **`0x8C157754`** (`E001A.WAV`, `E001B.WAV`…) e cria pelo `PlyCreateWav` (`0x8C158FC4`) | `voz/<NOME>` (o recurso original, ex. `voz/e002a`); **contexto** = o ponto que chamou + o índice na tabela dele (a cena) | **sim** |
| **Músicas** | `8C02AE70` (gerenciador de música, sequências `M*.MSB`) | `musica/<NOME>` | não |

Achados que importam para as falas:
- O 8C035E5A usa IDs 4, 5, 6, 7, 8… → `E002A.WAV`, `E002B.WAV`, `E003.WAV`, `E004A.WAV`, `E004B.WAV`
  (conferido no binário).
- **Mapa completo (fase E, `tools/dev/n6_vozes.py`):** a tabela `0x8C157754` tem **239 IDs** (0–238), 6 vazios
  (26, 87, 97, 98, 215, 217: o player ignora) e **233 nomes, um a um com os 233 WAV do disco** (nenhum faltando,
  nenhum sobrando). A suposição anterior, "26 contíguos e depois outro dado", estava errada: os 26 primeiros são só
  os `E0xx` em ordem. Formato: ADPCM da AICA, 22 050 Hz, 232 mono e 1 estéreo (`E084`), 1 421 s no total.
- **Nem todo WAV do player é fala:** os `*ENV.WAV` (24) são **ambientes** tocados pelo mesmo player (a rota de
  combate toca `E012AENV` e `E012BENV`). Eles também geram eventos (o ID diz o que são); uma legenda de mod só
  aparece se o pacote tiver texto para o ID.
- **Fala × música (lido no binário):** o `8C08B860` chama `8C02AE70(-48, 800)` antes do start (abaixa a música) e
  o fim (`8C08B980`, estado 3 ou 4, ou a cena) chama `8C02AE70(0, 800)` (devolve). Isso é lógica do jogo, que
  continua a original: o provedor nativo só precisa entregar o estado 3 no instante certo.
- **Uma fala por vez:** o player ignora o pedido enquanto `S+56` (fala ativa) está ligado; o contador `S+58` (15,
  -1 ao tocar) é testado pelo gameplay em `8C052BE8` e `8C083746`.
- **ID + cena + contexto:**
  - a legenda e a voz se ligam pelo `voz/<NOME>`;
  - a mesma voz pode ter legenda diferente por cena (`cena` no `falas.<idioma>.toml`; sem ela, a geral);
  - se a fase E mostrar o mesmo WAV em contextos diferentes, a cena passa a ser `<chamador>:<índice>`.
- **Nem todo áudio é fala:** os 211 efeitos e as músicas nunca geram eventos de diálogo.

## 7. Estrutura de arquivos

```
mods\
  traducao-ptbr\                 (um pacote = uma pasta com mod.toml)
    mod.toml                     formato = 1, id, nome, versao, prioridade, idiomas = ["pt-BR"], jogo (opcional)
    textos\pt-BR.toml            [textos] "id" = "texto"
    legendas\E001.pt-BR.srt      filmes: nome original + idioma (SRT ou WebVTT)
    legendas\falas.pt-BR.toml    [[fala]] voz, cena (opcional), inicio, fim, texto
    vozes\pt-BR\                 dublagem (hoje pelo SoundCatalog: mods\audio\<idioma>\<id>; a fase E unifica)
    fontes\                      fontes do pacote (o apresentador do N7 as usa)
```

Garantias **[preparado, testado]**:
- UTF-8 conferido;
- IDs estáveis;
- caminhos seguros: nada de `..`, absoluto ou junção que saia do pacote, pelo canônico;
- idiomas BCP-47 canônicos;
- **prioridade determinística**: maior `prioridade`, depois o `id`;
- recuo de idioma: o exato em qualquer pacote vence o recuo, e nunca entra outro idioma;
- recurso ausente = o original;
- `formato` mais novo que o suportado é recusado com motivo;
- `jogo` de outra versão é recusado;
- `id` repetido é recusado;
- sem o CDI.

**A unificar na fase E:** hoje vozes e filmes têm as pastas do N5/N6 (`mods\audio`, `mods\video`). Os pacotes
(`mods\<pacote>\`) são a forma nova. A fase E faz o `SoundCatalog` e o `MovieCatalog` consultarem também os
pacotes, mantendo as pastas antigas.

## 8. Sincronização

- **Filmes:** o tempo de **mídia** do player (100 ns), nunca FPS fixo. A pausa é do player (o tempo para); o fim e
  o pulo encerram as legendas; um filme de mod com outra duração usa a linha do tempo do próprio arquivo.
- **Falas:** o tempo **lógico** do jogo. A pausa congela o tempo decorrido da fala; fim, interrupção e troca de cena
  encerram; um evento repetido recomeça sem duplicar.
- **Dublagem com outra duração:** o jogo vê a duração original (sincronia `original`), e o relógio lógico não
  muda. A **legenda acompanha o áudio** (decisão da fase E, `DialogueBridge`): começa no `Started`, acaba no
  `PhysicalEnd` ou na interrupção; o `LogicalEnd` não a encerra (numa dublagem mais longa o áudio continua).
- O `SubtitleService` **nunca produz** eventos nem tempo: a sincronização lógica do gameplay não muda.

## 9. Integração futura com Vulkan (N7)

**[preparado]** `ISubtitlePresenter::present(blocos, estilo)`, sem dependência de Vulkan, DirectWrite ou Win32:
- texto Unicode (UTF-8 → UTF-32) e fonte externa (`SubtitleStyle::font`);
- escala pela altura do conteúdo, com a opção do jogador;
- contorno e sombra;
- margens seguras;
- quebra por palavras, e por caractere no japonês sem espaços;
- limite de linhas;
- área do conteúdo 4:3 numa tela de outra proporção;
- **sobe acima do HUD** (retângulos a evitar) e não sai da área segura.

A medida do texto vem do backend (`ITextMeasurer`). O mesmo layout serve sobre o filme e sobre o gameplay.
**[futuro, N7]** o apresentador Vulkan e as regiões reais do HUD.

## 10. Ferramentas previstas no Developer (N10)

O BlueStinger-Recompiler continua simples. No BlueStinger-Developer, no N10:
- editor visual e pré-visualização sincronizada de legendas;
- exportação dos textos originais;
- ferramentas de tradução;
- associação fala ↔ voz;
- gerenciador e empacotamento de pacotes de idioma;
- fontes;
- validação completa.

Hoje a inspeção de recursos do Developer já mostra filmes, mods e manifestos. No N10 ela passa a mostrar também
os pacotes (`mod.toml`).

## 11. Plano de testes

**[feito]** `bs_localization_tests` **6/6** (sintéticos, sem o jogo):

| Teste | Cobre |
|---|---|
| `loc_languages_ids_utf8` | BCP-47 válidos e inválidos, canônico, recuo; IDs válidos e inválidos; UTF-8 inválido (truncado, sobrelongo, substituto) |
| `loc_mod_packages_priority_and_safety` | prioridade e empate; formato novo, ilegível, idioma inválido, outra versão do jogo e id repetido recusados com motivo; recuo de idioma; recurso ausente; outro idioma nunca entra; caminhos inseguros; sem mods |
| `loc_text_localization` | exato > recuo, prioridade, ID inválido, sem tradução = original, códigos especiais preservados ou recusados |
| `loc_subtitles_movie_timeline` | tempo de mídia, pausa do player, japonês, pulo, fim, filme sem legenda, idioma inválido |
| `loc_subtitles_dialogue_events` | tempo lógico, pausa e retomada, ID + cena e a geral, evento repetido, interrupção, sem fim, troca de cena, evento solto, voz sem legenda, entradas inválidas |
| `loc_subtitle_layout` | escala, quebra, japonês por caractere, limite de linhas, 4:3 em 16:9, acima do HUD, área segura |

Duração da dublagem modificada: `audio_dub_duration_policy` e `audio_sd_logical_end_frees_port` (N6).

**[futuro]**
- fase E: evento de fala no jogo contra o oráculo (o mesmo passo da criação e do fim do WAV);
- N7: captura do desenho.

## 12. Riscos e dependências

1. **Códigos do motor de texto** e onde o jogo lê cada texto: sem eles a tradução de textos não liga (N10). O
   contrato está pronto.
2. **Mapa id → WAV:** resolvido (239 IDs, 233 nomes = 233 arquivos). Falta saber **quais IDs cada chamador usa
   em cada cena** (contexto para legendas por cena): oráculo de gameplay com falas.
3. **Fala × música:** lida no binário (abaixa -48 em 800 e devolve 0 em 800, pela lógica original). Medir o efeito
   audível é da fase F (música).
4. **Fontes com japonês e acentos:** a fonte do pacote ou uma padrão com cobertura. Escolha e licença no N7.
5. **Duas formas de pastas de mods** até a unificação da fase E.
6. **FLAC/OGG:** FLAC com `dr_flac` (feito). **OGG pendente** de decisão: `stb_vorbis` (preferência) tem os
   CVE-2023-45675…45682 (escritas fora dos limites com arquivo forjado) sem correção publicada (último commit
   2021); alternativas: `libvorbis`+`libogg` (Xiph, BSD-3, mantidas) ou o `stb_vorbis` com correções locais.
7. **Desenho das legendas no gameplay:** só no N7. Até lá, só os filmes têm legendas na tela (N5).

## 13. Primeiro pacote: legendas em inglês (2026-10-20)

Detalhes em `docs/LEGENDAS-EN-PLANO.md`. O que muda aqui:
- **existe um pacote real** no formato deste plano (`mods\legendas-en`, gerado pelo pipeline de
  `tools/dev/legendas/`, fora do Git): 685 linhas das falas (por `voz/<nome>`) e 8 filmes com fala; o parser do jogo
  o carrega sem problemas (`loc_mod_legendas_en_gerado`) e, com as falas nativas, as legendas ficam ativas no jogo
  (registradas no log; o desenho é do N7);
- a **transcrição** (`fontes/transcricoes.en-US.tsv`, com confiança e estado por linha) é a base da tradução PT-BR;
- **filmes:** enquanto as duas formas de pastas de mods existirem (risco 5), o pacote traz as legendas dos filmes
  também em `video\<NOME>.en.srt` (o leiaute do N5, que o player de filmes já desenha);
- descobertas que valem para as legendas: 3 `ENV.WAV` têm fala e 11 falas sem `ENV` não têm fala detectada (a
  categoria vem da detecção, não do nome); `E084.WAV` (ID 99) é ADPCM **estéreo** e ainda não toca no nativo.
