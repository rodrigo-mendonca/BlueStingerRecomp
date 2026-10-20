# N5: integração da worktree (preparada em 2026-10-19, durante a regressão do N4)

Complementa `docs/N5-PLANO.md` (seções 9, 13 e 14). Tudo abaixo está **na worktree**
`../recomp-BStinger-n5` (base `b1c76ec`), compilado e testado por bibliotecas. O executável
final do jogo ainda não foi gerado com os slots: é o primeiro passo depois do commit do N4.

## 1. Os dois achados ALTA da segunda revisão do Codex

**Ainda abertos**: só se resolvem com o executável final validado.

| Achado | O que foi feito na worktree |
|---|---|
| `video_adapter` fora do executável | `bs_video_adapter` (`video_adapter.cpp` sobre `bs_transitional` + `bs_video_mw`), ligado ao `bs_app_core`. `GameOptions::video` (`VideoProvider::Legacy` por padrão), `modVideo`, `subtitles`. O `GameHost` instala os slots antes do boot, troca o quadro exibido pelo do filme enquanto `playing()` e imprime o resumo no fim. Os 8 slots estão em `[native] slots` (`config/bluestinger_us.toml`). `--video nativo\|legado` e `--legendas <idioma>` no `main_sdl.cpp`. Som do filme pelo XAudio2 só em tempo real; no headless, saída nula com o mesmo relógio lógico. |
| Builder sem `build_movies` | `bs_builder_core` liga `bs_video_convert`. Depois dos dados conferidos, os filmes vão para `data\video` (reaproveitados por SHA-256; sem o codificador do Windows ficam ausentes e a compilação segue). `Request::movies` e `--sem-filmes`. `mods\video` do projeto vai para o pacote. A licença do `pl_mpeg` entra em `licencas\` da distribuição do Builder. |

## 2. Achado desta rodada: o servidor por handle do MW

Os despachantes que o laço do filme chama, `8C1745E6` (via `8C157F60`) e `8C174472`, percorrem as
tabelas de servidores em `8C2DA7D0`/`8C2DA6D0`. O servidor do MW (`8C158A82`, registrado por
`8C158CB8`/`8C158E24`) chama `8C1589F0`, que passa pelas **32 entradas do pool** e executa
`8C1588D8(handle)` em toda entrada com `+4 ≠ 0`. Ou seja, a entrada nativa também é executada.

Com `+64 = 0`, `8C1588D8` volta sem efeito: só lê `+52`, sem desreferenciar. O MW zera o pool na
inicialização (`8C158BA8`), o create original zera `+64` (`8C158060`) e o destroy original também
(`8C158130`).

**Correção:** o create nativo zera os 120 bytes da entrada antes de gravar `+0`/`+4`, para não
depender do histórico da entrada. O estado fica espelhado em `+8`, como no original. O contador
`serverExposed` acusa `+64 ≠ 0` na entrada nativa (tem que ficar 0). Testado em
`video_adapter_lifecycle`, com uma entrada cheia de restos.

**Chamadores:** create, parâmetro e cor base só são carregados dentro de `8C08B544` (literais
`8C08B6A8`, `8C08B6AC`, `8C08B6B4`). O offset também em `8C08B726`, na mesma função. Os únicos `bsr`
diretos para as 8 entradas são os dois para o stop, dentro do destroy e do start originais, que só
rodam para handles alheios. A cola não captura players de outros chamadores.

## 3. Testes (`bs_video_tests`, 36/36)

| Pedido | Teste |
|---|---|
| Inicialização do adaptador MW | `video_slot_install_and_refusal`: 8 slots no nativo, desinstalação volta ao gerado, recusa sem `[native] slots` |
| Vídeo sintético | `video_slot_playback_without_sofdec`, `video_adapter_lifecycle` |
| Fim e retorno | estado 3, stop e destroy devolvem 0, `+4` livre; o retorno **ao gameplay** só no jogo |
| Vídeo ausente | `video_slot_missing_movie`, `video_adapter_missing_then_restart` |
| START permitido e proibido | decidido pela função original (intacta). Testável só no jogo: rota com START durante o E001 |
| Falha de decodificação | `video_mods_incompatible_file` |
| Áudio indisponível | `video_adapter_audio_failure_and_recovery` |
| Mod | `video_mods_replace_and_fallback`, `video_mods_subtitle_language_fallback` |
| Liberação de recursos | saída de áudio fechada (`video_adapter_lifecycle`, `..._audio_failure_and_recovery`), `+4 = 0` no destroy |
| Nenhuma chamada Sofdec durante o filme nativo | `video_slot_playback_without_sofdec`: os 8 originais contados, 0 chamadas. No jogo: `serverExposed = 0` e "chamadas de outros players" no resumo `[video]` |

## 4. Mesclagem

Só dois arquivos se sobrepõem às mudanças do N4 ainda sem commit: `app/game_host.h` e
`app/main_sdl.cpp`. São trechos vizinhos (opções de saves e de vídeo), sem lógica em comum.

**Ordem:**
1. Commit do N4 na árvore principal.
2. Commit do N5 na worktree, numa branch (`n5-videos`), sobre `b1c76ec`.
3. Na árvore principal, `git cherry-pick` (ou rebase da branch sobre o commit do N4). Resolver à
   mão os dois arquivos, mantendo os saves nativos como padrão (N4) e o vídeo legado como padrão
   (N5 em validação). Conflitos esperados (hunks adjacentes, conferidos em 2026-10-19), todos do
   tipo "manter os dois lados":
   - `app/game_host.h` ~l.57-77: o N4 troca o padrão de `saves` (comentário e
     `SavesProvider::Native`); o N5 insere `VideoProvider` e os campos `video`/`modVideo`/`subtitles`
     logo depois.
   - `app/main_sdl.cpp` `Args` ~l.65-71: o padrão de `saves` (N4) mais os campos `video`/`subtitles`
     (N5).
   - `app/main_sdl.cpp` `parseArgs` ~l.117-127: o N4 acrescenta `--saves-importar/exportar/nome`;
     o N5, `--video`/`--legendas`.
   - `app/main_sdl.cpp` `kUsage` ~l.685-689: o texto dos saves (N4) mais as duas linhas do vídeo
     (N5).

   Sem conflito: `game_host.cpp` (o N4 não o altera), `gameOptions()` e o resto.
4. Regenerar o código (`bs-recomp`, os 8 slots), compilar release e mods.
5. Testes afetados:
   - `ctest` e unitários;
   - `bs_video_tests`;
   - `test_builder` (com filmes) e `test_projeto` (`mods\video`);
   - `test_preview`;
   - a rota até a área 1 com `--video legado` (o gerado nos slots não muda nada) e com
     `--video nativo` (E001).

   **Custo:** com o passo dos filmes, cada compilação do Builder converte os 16 SFD na primeira
   vez (minutos). As suítes que não tratam de vídeo (`test_builder` e `test_projeto`, fora um caso
   com filmes) passam `--sem-filmes`. A Preview (`make_preview`) converte: é a distribuição.
6. **Primeiro E001:** `powershell -File tools/dev/n5_e001.ps1`. Monta uma pasta de dados própria
   com hard links da Preview e converte só o E001. Verificações:

   | | Verificação |
   |---|---|
   | V0 | conversão |
   | V1 | as rotas legado e nativo terminam sem erro |
   | V2 | E001 aberto pelo manifesto |
   | V3 | fim natural em 13 700 ± 100 ms lógicos, ~411 quadros |
   | V4 | entrada nunca exposta ao servidor do MW |
   | V5 | o nativo não lê o `E001.SFD` |
   | V6 | pós-filme comparado por passo com o legado (`compara_modos.py --de`) |
   | V7 | ≥ 380 trocas de quadro na janela do filme |
   | V8 | START durante o filme (`ctl=01`) o encerra e o jogo segue |

   O START entra por passo se o passo anda durante o filme; senão, pela leitura do controle. O
   som real (XAudio2) só se ouve com janela: `--video nativo` sem `--headless`, na mesma pasta de
   dados.

**Preserva:** controles nativos (N3) e saves nativos (N4) como padrão. O vídeo nativo só com
`--video nativo` até o E001 ser validado no jogo.

**Documentação:** `STATUS.md`, `docs/N5-PLANO.md` e este arquivo entram juntos no commit de
integração.

## 5. Terceira revisão do Codex (`reviews/codex-20261019-175202.md`, na worktree): 2 achados, ambos aceitos

| Gravidade | Achado | Correção |
|---|---|---|
| MÉDIA | `--sem-filmes` deixava no pacote os MP4 e o manifesto de uma compilação anterior na mesma saída | `clear_movies` apaga `data\video` (os pacotes anteriores têm hard links próprios); teste em `video_build_without_encoder` (manifesto válido → apagado → catálogo sem o filme) |
| BAIXA | `serverExposed` fora do resumo; o teste de slots não o conferia | no resumo `[video]` ("entrada exposta ao servidor N"); `video_slot_playback_without_sofdec` confere 0 e acusa um `+64` plantado. A prova pelo servidor real (`8C158A82 → 8C1589F0 → 8C1588D8`) fica para a rota no executável final |

A análise do servidor por handle (seção 2) foi conferida pelo Codex no binário (`8C158A90` chama
`8C1589F0`; `8C158A0C`–`8C158A14` chamam `8C1588D8` para cada entrada com `+4 ≠ 0`).
`bs_video_tests`: 36/36.
