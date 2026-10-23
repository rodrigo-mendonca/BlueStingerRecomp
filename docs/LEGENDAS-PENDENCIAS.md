# Legendas: revisões pendentes e direitos de redistribuição

Situação em 2026-10-23. Pacotes locais (gerados do disco do usuário, fora do Git): `legendas-en` **0.2.0** e
`legendas-pt-br` **0.1.0**, 734 linhas cada (691 falas do gameplay e 43 dos filmes). O Builder os instala em toda
instalação quando o kit os leva (`make_builder.ps1 -LegendasLocais`); o jogo escolhe pt-BR, depois en-US, depois
desligadas (`--legendas auto`, padrão).

Este documento lista só as **chaves** das linhas (recurso, id e linha). O texto delas é derivado das falas do jogo e
fica nos pacotes locais (`fontes/transcricoes.en-US.tsv`, `fontes/pendentes.pt-BR.tsv`).

## Quem revisou o quê

Todas as revisões feitas até aqui são **por IA** (Claude e Codex). **Nenhuma linha foi revisada por humano.**

| Pacote | Estado das linhas |
|---|---|
| en-US 0.2.0 | 609 automáticas (ASR com confiança alta), 53 revisadas por IA, **72 a revisar**, 11 excluídas (sem fala) |
| pt-BR 0.1.0 | 734 traduzidas por IA; o Codex revisou as 734 em 9 lotes: 667 ok, 33 aceitas, 5 ajustadas, 5 rejeitadas com motivo, **24 pendentes** |

Relatório da revisão do Codex: `reviews/legendas-pt-20261023-0108/relatorio.md` (local, ignorado pelo Git).

## en-US: 72 linhas marcadas "revisar"

ASR com confiança baixa ou nome/termo duvidoso. Precisam de alguém ouvindo o áudio. Formato: recurso, quantidade,
`id:linha` (filmes: linha).

| Recurso | Qtd. | Linhas |
|---|---|---|
| `filme/e180` | 1 | 0 |
| `filme/e185a` | 1 | 0 |
| `filme/newgame` | 2 | 0, 9 |
| `voz/e001c` | 1 | 2:0 |
| `voz/e001d` | 1 | 3:0 |
| `voz/e002a` | 2 | 4:0, 4:3 |
| `voz/e002b` | 1 | 5:8 |
| `voz/e007a` | 1 | 14:0 |
| `voz/e007b` | 1 | 15:0 |
| `voz/e008` | 2 | 17:0, 17:2 |
| `voz/e012` | 1 | 22:3 |
| `voz/e017b` | 1 | 230:3 |
| `voz/e017c` | 1 | 231:10 |
| `voz/e024` | 1 | 154:1 |
| `voz/e026` | 1 | 34:0 |
| `voz/e027` | 2 | 35:0, 35:1 |
| `voz/e028` | 1 | 36:0 |
| `voz/e029` | 3 | 37:0, 37:1, 37:2 |
| `voz/e030` | 1 | 38:0 |
| `voz/e031` | 2 | 39:0, 39:1 |
| `voz/e032` | 1 | 157:1 |
| `voz/e035` | 1 | 42:0 |
| `voz/e036` | 1 | 43:0 |
| `voz/e037` | 1 | 44:1 |
| `voz/e038` | 1 | 45:0 |
| `voz/e047` | 1 | 52:8 |
| `voz/e048` | 2 | 163:1, 163:3 |
| `voz/e053` | 1 | 54:0 |
| `voz/e054` | 1 | 55:0 |
| `voz/e055b` | 1 | 57:0 |
| `voz/e056` | 2 | 60:4, 60:5 |
| `voz/e066` | 1 | 175:0 |
| `voz/e070` | 1 | 177:0 |
| `voz/e077a` | 1 | 84:0 |
| `voz/e078c` | 1 | 88:1 |
| `voz/e080` | 1 | 91:12 |
| `voz/e087` | 1 | 180:3 |
| `voz/e089a` | 1 | 101:0 |
| `voz/e089b` | 1 | 102:0 |
| `voz/e089b_a` a `voz/e089b_g` | 7 | 188:0 a 194:0 |
| `voz/e089b_k` | 1 | 198:0 |
| `voz/e089b_m` | 1 | 200:0 |
| `voz/e092c` | 1 | 106:0 |
| `voz/e097a` | 1 | 112:0 |
| `voz/e098c` | 1 | 116:0 |
| `voz/e099` | 1 | 118:0 |
| `voz/e101b` | 1 | 121:0 |
| `voz/e104` | 2 | 169:0, 169:2 |
| `voz/e108` | 1 | 131:0 |
| `voz/e109` | 1 | 132:0 |
| `voz/e110` | 1 | 133:0 |
| `voz/e121` | 1 | 144:0 |
| `voz/e122` | 1 | 145:0 |
| `voz/e123` | 1 | 146:0 |
| `voz/e124` | 1 | 147:0 |
| `voz/e128` | 1 | 209:0 |

## pt-BR: 24 linhas pendentes

O Codex apontou e a correção depende do áudio (o inglês de origem é duvidoso). O texto atual foi **mantido**, sem
correção inventada. Chaves (recurso e linha do arquivo):

`filme/newgame 0`, `voz/e002a 3`, `voz/e026 0`, `voz/e027 0`, `voz/e030 0`, `voz/e032 1`, `voz/e035 0`, `voz/e036 0`,
`voz/e038 0`, `voz/e047 8`, `voz/e048 3`, `voz/e056 4`, `voz/e056 5`, `voz/e066 0`, `voz/e070 0`, `voz/e080 12`,
`voz/e087 3`, `voz/e089a 0`, `voz/e089b 0`, `voz/e089b_a 0`, `voz/e104 0`, `voz/e104 2`, `voz/e123 0`, `voz/e128 0`.

As 24 estão todas entre as 72 inglesas a revisar: revisar o inglês no áudio primeiro e depois retraduzir (`tools/dev/legendas/gerar_traducao.py`).

## Como fechar uma pendência

1. Ouvir o áudio (`game-data`, recurso e tempo da linha em `fontes/transcricoes.en-US.tsv`).
2. Registrar a correção em `tools/dev/legendas/revisoes.en-US.tsv` (local) com o tipo `humana` (ou `ia`, se for o
   caso, nunca alegando revisão humana) e regenerar com `gerar_mod.py`.
3. Para o pt-BR, regenerar a tradução e marcar a decisão em `aplicar_revisao.py`.
4. Subir a versão do pacote no `mod.toml` e registrar no `STATUS.md`.

## Direitos de redistribuição (verificação de 2026-10-23)

O que foi verificado:
- As falas, as vozes e os filmes são do jogo comercial (© Climax Graphics / Sega / Activision; ver `LEGAL.md`).
- As transcrições em inglês reproduzem o texto falado no jogo; a tradução pt-BR é obra derivada desse texto.
- O projeto **não tem** licença, permissão ou autorização dos detentores para publicar esse texto, nem para publicar
  traduções dele. Nada no repositório, no disco ou na documentação do jogo concede isso.

Conclusão: **os pacotes não podem ir para o GitHub nem para uma distribuição pública** sem uma autorização adequada,
que este projeto não tem. Eles continuam:
- fora do Git (`mods-padrao/*/` ignorado; só `mods-padrao/README.md` com o processo é versionado);
- na distribuição **local** de testes, gerada a partir do disco do próprio usuário (`make_builder.ps1 -LegendasLocais`,
  com o aviso no `VERSAO.txt`).

O caminho publicável é o que já existe: as **ferramentas** (`tools/dev/legendas/`, MIT) vão para o Git e cada usuário
gera os pacotes a partir da própria cópia do jogo. Publicar os pacotes prontos exige, antes, a autorização dos
detentores ou uma avaliação jurídica, e uma decisão explícita do mantenedor.

Pendência do histórico: o commit local `2969913` (sem push) contém o texto inglês 0.1.0. Antes de qualquer push ele
precisa ser removido do histórico, o que só é feito com autorização do mantenedor
(`docs/PUBLICACAO-GITHUB.md`).
