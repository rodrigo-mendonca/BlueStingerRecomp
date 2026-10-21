# Mods de vídeo e legendas (N5)

Os filmes do Blue Stinger Native PC podem ser trocados por versões remasterizadas (1080p, 4K, outra taxa de
quadros) e ganhar legendas em qualquer idioma, **sem recompilar** o jogo. Basta colocar os arquivos em
`mods\video\` ao lado do `BlueStinger.exe`.

## Como o jogo encontra um filme

Cada filme tem um **ID** (0 a 16) e um **nome original** do disco (por exemplo, `NEWGAME.SFD`). Ao tocar,
o jogo procura, nesta ordem:

1. **`mods\video\manifest.toml`**: uma entrada `[[filme]]` com o mesmo `id`;
2. **`mods\video\<NOME>.mp4`**: o nome original trocando `.SFD` por `.mp4` (`NEWGAME.mp4`);
3. **`data\video\`**: o filme convertido pelo Builder a partir do seu disco. É a referência de
   fidelidade.

| ID | Original | ID | Original |
|---|---|---|---|
| 0 | `OPENING.SFD` (não existe no disco americano: um mod pode fornecê-lo) | 9 | `E193.SFD` |
| 1 | `NEWGAME.SFD` | 10 | `E195.SFD` |
| 2 | `E017.SFD` | 11 | `E200A.SFD` |
| 3 | `E001.SFD` | 12 | `E200B.SFD` |
| 4 | `E107.SFD` | 13 | `ENDING.SFD` |
| 5 | `E179.SFD` | 14 | `E016.SFD` |
| 6 | `E180.SFD` | 15 | `E134.SFD` |
| 7 | `E185A.SFD` | 16 | `ENDING2.SFD` |
| 8 | `E185B.SFD` | | |

## Formatos

- **Vídeo e áudio de mods:** qualquer arquivo que o **Media Foundation** do Windows decodifique,
  com qualquer resolução, taxa de quadros (inclusive variável) e duração.
  - MP4 com H.264 ou H.265, MPEG-4, WMV, AAC, MP3 e outros, conforme os codecs instalados.
  - Sem decodificador para o arquivo, o log diz qual filme falhou e por quê, e o jogo segue como
    se o filme tivesse terminado com erro.
- **Conversão do Builder:** MP4 com H.264 (qualidade máxima do codificador do Windows) e AAC a
  192 kbit/s. Esse teto do AAC é do codificador que o Builder usa, não da arquitetura: um mod pode
  trazer áudio em qualquer formato aceito acima.
- **Legendas:** SRT ou WebVTT em UTF-8. Itálico, negrito, cores e posições são ignorados; o texto
  sai branco com contorno, centralizado embaixo e proporcional à altura do vídeo.

## Legendas por idioma

`mods\video\<NOME>.<idioma>.srt` ou `.vtt`, por exemplo `NEWGAME.pt.srt` ou `E001.en.vtt`. O jogo usa
o idioma escolhido em `--legendas <idioma>` ou no `instalacao.toml` (`[legendas] idioma`); o padrão é `en-US`
(recua para `.en`), e `--legendas nenhuma` desliga. Sem arquivo no idioma, não mostra legendas. O jogo original não tem
legendas: todas vêm de mods, inclusive traduções novas.

## Manifesto de mods (opcional)

`mods\video\manifest.toml` dá controle total, por ID:

```toml
[[filme]]
id = 1
arquivo = "intro_remaster_4k.mp4"          # relativo a mods\video, sem sair da pasta
legendas = { pt = "intro.pt.srt", es = "intro.es.vtt" }

[[filme]]
id = 0                                      # o OPENING, ausente no disco americano
arquivo = "opening_fanmade.mp4"
```

- Caminhos absolutos ou com `..` são **recusados** (registrados no log).
- Um manifesto com erro de sintaxe é ignorado, com aviso no log; os filmes do Builder continuam
  valendo.

## O que muda no jogo

- **Modo fiel** (sem mod): o jogo vê o mesmo contrato do original, com os mesmos estados, códigos
  de retorno e pulo com START onde o original permite.
- **Modo modificado** (mod de outra duração): o filme termina quando o arquivo do mod termina. O
  jogo só espera o fim, o pulo ou a interrupção, então a nova duração não quebra nada.
- O pulo com START continua valendo só nos filmes em que o jogo original o permite.
- O áudio do filme sai pelo dispositivo padrão do Windows. Se ele for perdido no meio (fone
  desligado), o filme continua sem som.
