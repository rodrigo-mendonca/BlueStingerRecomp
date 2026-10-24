# Legendas: revisões pendentes e direitos de redistribuição

Situação em 2026-10-23. Pacotes locais (gerados do disco do usuário, fora do Git): `legendas-en` **0.3.0** e
`legendas-pt-br` **0.2.0**, as mesmas 734 linhas (691 falas do gameplay e 43 dos filmes) do 0.2.0/0.1.0, com os tempos
ressincronizados pela voz audível e 23 linhas divididas em partes (seção "Sincronia"). O Builder os instala em toda
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

## Sincronia (en 0.3.0 / pt-BR 0.2.0, 2026-10-23)

Problemas relatados pelo jogador: legendas segundos antes da fala e duas falas seguidas numa legenda só. Causa nos
pacotes (medida contra a energia do próprio áudio de cada fala): o ASR esticava a primeira palavra sobre o silêncio
anterior (72 linhas começavam > 0,5 s antes da voz, até 7,08 s; na reprodução, até 12 s quando a linha começava na
sobra da fala anterior) e só dividia as linhas por tamanho, nunca nas pausas (48 linhas cobriam duas falas separadas
por silêncio). Correção sem mudar texto nem chaves (`tools/dev/legendas/ressincronizar.py`, `sincronia.py`):
início e fim pela voz que a linha cobre; partes nas pausas, na fronteira de frase ou na palavra exata.

| Medida | 0.2.0 | 0.3.0 |
|---|---|---|
| linhas que começam > 0,5 s antes da voz | 72 (máx. 7,08 s) | 3 (máx. 0,61 s) |
| > 1 s antes | 5 | 0 |
| linhas divididas em partes | — | 23 (26 partes a mais) |

Pendências que ficam (só as chaves):
- **sem corte seguro** (a pausa cai no meio de uma frase de uma mesma pessoa, ou o texto não alinha às palavras do ASR;
  ficam inteiras): `voz/e026 0`, `voz/e080 3`, `voz/e105 0`, `voz/e089b_a 0` a `voz/e089b_d 0`, `voz/e089b_f 0`,
  `filme/e193 0`; sem trecho audível que a linha cubra (tempos mantidos): `voz/e022b 13`;
- **pt-BR sem divisão** (o número de frases da tradução não bate com o do inglês; a linha pt-BR aparece inteira do
  início da primeira parte ao fim da última): `voz/e003 5`, `voz/e003 9`, `voz/e006a 3`, `voz/e080 13`, `voz/e096 5`,
  `voz/e017c 5` (`fontes/partes-sem-divisao.pt-BR.tsv`);
- **tradução incompleta:** em `voz/e080 13` o pt-BR não traduz a última frase do inglês (achado na sincronia; não
  corrigido: depende de revisão);
- **identificação do personagem:** nenhum pacote traz o campo `personagem`; o jogo e o catálogo não têm metadado
  confiável de quem fala em cada linha. A opção do menu fica indisponível até haver essa informação (nada por
  suposição).

## Filmes (en 0.3.1 / pt-BR 0.2.1, 2026-10-23)

Relato do jogador: falas seguidas dos filmes numa legenda só. **Causa:** a ressincronização de 0.3.0 usou a energia
do áudio para achar a voz; nos filmes a trilha tem música e efeitos quase o tempo todo, então o filme inteiro parecia
"audível": nenhuma pausa, cada linha ia até a seguinte (até 95 s numa linha) e falas separadas por silêncio ficavam
juntas. Os pacotes 0.3.0/0.2.0 instalados em `mods\video` eram esses.

**Correção** (só os filmes; texto, chaves, revisões e as falas do gameplay iguais, `falas.*.toml` idênticos byte a
byte): os trechos de voz dos filmes vêm das palavras do ASR (`sincronia.palavras_corrigidas`, `trechos_das_palavras`),
com dois defeitos conhecidos corrigidos só na primeira palavra de cada segmento do ASR (esticada sobre o silêncio
anterior, ou posta segundos antes da frase dela: 5,3 s entre as duas primeiras palavras de uma frase do E185A); corte
no meio de uma frase só com 1,5 s de silêncio (`ressincronizar.py --so-filmes`). No jogo, a legenda do filme segue a
posição **audível** (a posição do filme menos a fila da saída, monotônica), como as falas.

| Medida (por idioma; `tools/dev/n5_legendas_filmes.py --pacotes`) | 0.3.0 / 0.2.0 | 0.3.1 / 0.2.1 |
|---|---|---|
| filmes com defeito (dos 6 com legenda) | 5 | 0 |
| linhas que entram antes da voz (> 0,1 s) | 21 | 0 |
| linhas que ficam > 1,5 s depois da voz | 15 | 0 |
| linhas com duas falas separadas por ≥ 1,5 s | 4 | 0 |
| NEWGAME na janela (en e pt-BR): entrada pelo áudio | antes da voz até a fila (133–500 ms) | 0 a +0,2 s depois do início da voz, cada linha uma vez (sem piscar); numa de três execuções uma linha entrou a +0,31 s, junto de um descarte de áudio do filme (fila atrasada pelo host), e não se repetiu |

Pendências que ficam:
- as linhas do pacote que já terminavam no meio de uma frase continuam assim (ex.: duas linhas do E195 que dividem uma
  frase ao meio): é a divisão do texto do pacote, não do tempo; refazê-la é revisão de texto;
- as 72 linhas en e as 24 traduções pt-BR "revisar" continuam como acima;
- a medida no jogo só cobre o NEWGAME (o único filme alcançado pelas rotas); os outros cinco foram conferidos pelos
  pacotes contra as palavras do ASR.

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
