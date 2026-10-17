# N2b-2: cobertura de gameplay além da primeira área

Data: 2026-10-17. Base: `8bfa94e` (N2b-2 experimental). Escopo autorizado: ampliar a validação
funcional do jogo e comparar `--tempo nativo` (estável, padrão) com `--tempo experimental`, sem
N2c nem N3–N7, sem mudar o escalonador nem o modo padrão.

## 1. Infraestrutura de gameplay assistido (só desenvolvimento)

Os roteiros de teste existentes (`newgame_area1`, `newgame_exploracao`) só chegavam à área 1. Para
avançar no jogo foi preciso jogar de fato: olhar a tela, decidir e reproduzir.

### 1.1 Peças

| Peça | O que faz |
|---|---|
| `app/assist.h/.cpp` | `BS_ASSISTIDO=<pasta>` com `--headless`. O jogo pausa entre VBlanks e atende comandos acrescentados em `<pasta>/comandos.txt`: `pad`, `passos`, `vblanks`, `ate_passo`, `roteiro`, `quadro` (PNG), `ram`, `dump`, `marca`, `fim`. Grava a sessão em `<pasta>/sessao.txt` |
| `GameOptions::vblankHook` | Gancho de diagnóstico na thread do jogo, depois do quadro exibido. Pausar ali não muda o jogo (headless, sem ritmo de tempo real) |
| `runtime/input`: roteiro `indice passo`, `LiveScriptInput`, `pad_state_from_text/text` | Entrada indexada pelo **passo de jogo** (em vez da leitura do controle); gatilhos e analógico no roteiro |
| `transitional/time`: `Stats::gameSteps`, `game_steps()`, `set_step_observer()` | Contador de passos de jogo: saídas da espera do VBlank do jogo (`8C064A2C`), uma por volta do laço principal |
| `BS_FRAME_LOG` coluna `passo`; `BS_STATE_PASSO` | Quadro por VBlank com o passo; estado lógico (hash das variáveis do jogo, vida, posição) no início de cada N passos |
| `tools/dev/assistido.py` | Cliente: `iniciar`, `cmd` (espera a pausa), `estado`, `pos`, `floats`, `mosaico` |
| `tools/dev/assistido_ir.py` | Navegação até (x, z) pelo modelo do controle relativo à câmera |
| `tools/dev/compara_modos.py` | Comparação de duas execuções por passo de jogo (quadros, leituras, salas, estado, áudio) |
| `tools/dist/test_assistido.ps1` | Teste A1–A5 sobre os roteiros `tools/dist/roteiros/passo_*.txt` |

Nada disso emula hardware: entrega o estado do controle (como o roteiro que já existia), captura o
quadro exibido e lê a RAM do guest. Fica atrás de variáveis de ambiente de diagnóstico, como
`BS_INPUT_SCRIPT` e `BS_RAM_DUMP`, e não muda a execução sem elas.

### 1.2 O índice por passo de jogo

O roteiro antigo é indexado pelo número de leituras do controle (`GetCond`). O estável lê o controle
durante as cargas (que lá duram VBlanks) e o experimental não (cargas instantâneas, D11), então o
mesmo índice cai em pontos lógicos diferentes: 13 leituras a mais no boot e 26 a mais até a área 1
(o −26 do N2b-2).

O **passo de jogo** é a saída da espera do VBlank do jogo (`f_8C064A20`, `BS_WAIT_END` em
`8C064A2C`): uma por volta do laço principal, a 30 Hz no gameplay, e nenhuma durante as cargas.
Medido nas rotas NEW GAME de 16 000 VBlanks: com o roteiro antigo os passos diferiam por um
deslocamento constante de 6, nascido no START do título (o roteiro antigo aperta START 6 passos
antes no estável); quadro a quadro, por passo, o boot coincide com deslocamento 0 e todo o resto
com deslocamento 6 (900 de 900 passos nos menus, 1 500 de 1 500 nos vídeos e cargas). Método: as
duas rotas (`newgame_exploracao` no estável, `newgame_exploracao_n2b2` no experimental) com a coluna
`passo` no `BS_FRAME_LOG`; para cada faixa, o deslocamento d que maximiza os passos s com quadro(s)
no estável = quadro(s+d) no experimental (análise local, artefatos fora do repositório). Com o
roteiro indexado por passo não há deslocamento nenhum: a mesma entrada vale nos dois modos sem
ajuste (seção 3, agora como critério do teste).

### 1.3 Reprodução

A entrada ao vivo vale do passo seguinte em diante (`pad` no passo `s` grava `s+1`), exatamente
como o roteiro gravado é lido. Verificado: a sessão ao vivo de 10 484 VBlanks e a reprodução da
`sessao.txt` com `BS_INPUT_SCRIPT` têm o mesmo `BS_FRAME_LOG`, linha a linha; duas reproduções
experimentais são idênticas (quadros, leituras, áudio). Isso virou o caso A8 do teste. Um `pad`
com entradas futuras de um `roteiro` carregado é recusado (A8b): antes era adiado até a última
entrada carregada (achado do Codex; nenhuma sessão gravada caiu nesse caso, todas rodaram até o fim
do roteiro antes do primeiro `pad`).

### 1.4 Controle do jogo (medido)

O direcional é **relativo à câmera**: com ângulo `atan2(z, x)`, UP anda na direção da câmera, RIGHT
a +90°, LEFT a −90°, DOWN a 180°; o personagem gira com inércia e a câmera o segue. Medido em 11
toques de 8 passos (direção do deslocamento menos a direção da câmera): UP 0°, 0°, +16°, −4°, +30°;
RIGHT +76°, +93°; LEFT −77°, −92°; DOWN +153°, −179° (o primeiro toque de cada direção sai menor
pela inércia). Endereços
usados (só leitura), identificados por comparação de dumps (`dump` do gameplay assistido) e não pelo
código do jogo; servem a navegação e diagnóstico, não a lógica:

| Endereço | Conteúdo | Como foi identificado |
|---|---|---|
| `0x8C24D668` | posição do Eliot (x, y, z, float) | triplas de float que andam com UP e param com o pad solto; y sobe ao subir no cais |
| `0x8C24CC78` / `0x8C24CC84` | posição da câmera / vetor de visão (float) | o vetor aponta da câmera para o Eliot (normalizado, confere com as posições) |
| `0x8C24FBCC` | vida do Eliot (int16) | correlação com a barra do HUD em 8 capturas (25 → 20 com a barra de 99 → 79 px); chega a 0 no GAME OVER |
| `0x8C1F2954` | medidor do monstro do heliporto | cai com os socos e se recompõe; não é a vida |

## 2. Áreas e mecânicas alcançadas

Jogado no gameplay assistido (sessões gravadas e guardadas como `tools/dist/roteiros/passo_*.txt`).

| Mecânica | Onde | Resultado |
|---|---|---|
| Transição de cenário e carga de área | cais (`RCG00`/`RCG00A`) → porta do C-GATE → corredor `RCG02` (música `M2A`, objetos `OST*`) → exterior/heliporto `RCG03` (`DCG03A/B`, `EMD00O`, `OID00A`...); depois do GAME OVER, título e NEW GAME de novo (`RCG00`) | 4 salas distintas, 6 cargas de sala por rota, no mesmo passo nos dois modos |
| Movimentação e câmera | cais (subir no cais elevado), corredor, heliporto; câmeras fixas e de perseguição | Controle relativo à câmera medido (1.4); navegação automática até (x, z) funciona |
| Interação com objetos e portas | porta do C-GATE (encostar dispara evento e abre), porta do corredor (A no painel abre), porta C01 do elevador (A: "The door is locked", painel "CHECK YOUR ID") | Funciona; a porta C01 exige um cartão de ID ainda não obtido |
| Inventário e uso de itens | menu START (MAP, ITEMS → FOOD/OTHER, WEAPON, CHANGE); "Eliot's Bank Card" em OTHER; mapa "Bay Area"; item achado junto ao guarda ("You found: Small Hassy"); uso ("Would you like to have a Small Hassy now? Use") | Funciona |
| Combate, inimigos e dano | monstro do heliporto (o corpo se levanta): ataques do monstro, sangue, vida do Eliot caindo (35 → 0, HUD laranja), socos do Eliot com A (reação do monstro, medidor em `0x8C1F2954`), morte, "GAME OVER", volta ao título | Funciona; o monstro não foi derrotado (ver 5) |
| Eventos de história e cinemáticas | cena no cais ("Press the START button"), rádio "Direct Radio Link"/"Contact lost" (duas vezes), cena com o Dogs, vídeos do NEW GAME | Funciona |
| Salvamento e carregamento (legado) | "VMU not found" no boot; LOAD GAME → Socket 1 → "A VMU is not inserted"; OPTIONS (dificuldade, áudio) | Comportamento legado: sem VMU no Maple não há save nem load (dependência para o N4) |

## 3. Equivalência entre os modos (`tools/dist/test_assistido.ps1`, 28 casos, 0 falhas)

Casos: A1 quadros, A2 leituras, A3 áudio, A4 estado lógico, A5 progresso e A7 cargas de sala por
roteiro (4 roteiros); A6 e A6b negativos (entrada deslocada, passo ausente de um log); A8 sessão
ao vivo = reprodução (2 375 VBlanks) e A8b `pad` recusado com entradas futuras.

Cada roteiro roda no estável (`--tempo nativo`) e no experimental, com `BS_FRAME_LOG`,
`BS_FILE_LOG`, `BS_AICA_LOG_TUDO` e `BS_STATE_PASSO` (a cada 10 passos), relógio fixo.

| Roteiro | Passos comparados | Quadros (fim de cada passo) | Leituras (blocos) | Áudio (escritas na AICA) | Estado lógico |
|---|---|---|---|---|---|
| `passo_cgate` | 0–9 287 | 9 288 iguais | 9 480, mesma sequência | 1 147 418, mesma sequência | 928 de 928 iguais |
| `passo_heliporto` | 0–14 328 | 14 329 iguais | 15 887, mesma sequência | 1 172 010, mesma sequência | 1 432 de 1 432 iguais |
| `passo_combate` | 0–12 983 | 12 984 iguais | 11 728, mesma sequência | 1 147 418, mesma sequência | 1 298 de 1 298 iguais |
| `passo_load_opcoes` | 0–3 200 | 3 201 iguais | 4 288, mesma sequência | 973 834, mesma sequência | 320 de 320 iguais |

- A6 (negativo): as entradas do menu atrasadas 3 passos só no experimental são acusadas
  (`quadros=FALHA`, `estado=FALHA`; leituras e áudio não mudam com a navegação do menu).
- Salas carregadas no mesmo passo nos dois modos: `RCG00` 1039, `RCG00A` 2147, `RCG02` 7337,
  `RCG03` 7757, `RCG00` 13207, `RCG00A` 14315.
- O menu de itens, que no N2b-2 era a única janela de quadros diferentes (86 trocas), agora coincide:
  a diferença vinha do índice por leitura dentro do menu (que carrega modelos e, no estável, lê o
  controle durante a carga), não da lógica.
- Sessão ao vivo = reprodução (1.3); determinismo do experimental: duas reproduções idênticas.

## 4. Divergências de carregamento e temporização (classificadas)

Duração em VBlanks de cada passo de jogo na rota `passo_heliporto` (29 mil VBlanks):

| Classe | Passos | Estável | Experimental | Natureza |
|---|---|---|---|---|
| Passo normal | 14 307 / 14 317 | 2 VBlanks | 2 VBlanks | igual |
| Boot (antes do 1º passo) | 0 | 33 | 4 | D11: o boot não consome VBlanks |
| Vídeo (NEW GAME e abertura) | 1038, 2146, 13206, 14314 | 144–160 | 142–150 | início do vídeo mais curto (o buffer enche sem esperar) |
| Carga de sala | 1039, 2147, 7337, 7757, 13207, 14315 | 5–9 | 2 | D11: carga instantânea |
| Outros passos longos | 2145, 12415, 14313 | 5–6 | 5 | iguais ou 1 VBlank |
| Total até o passo 14 328 | | 29 341 VBlanks | 29 250 VBlanks | o estável fica 91 VBlanks atrás |

Todas são de tempo (VBlanks por passo), nenhuma de conteúdo: quadros, leituras, áudio e estado no
mesmo passo são iguais.

## 5. Bloqueios para avançar na campanha

1. **Porta C01 (elevador) trancada:** pede um cartão de ID ("CHECK YOUR ID"). Ele não estava no
   item junto ao guarda (era a Small Hassy). Falta achar o cartão (provavelmente com o monstro do
   heliporto ou em outro corpo) ou outro caminho.
2. **Combate desarmado:** o monstro do heliporto não foi derrotado com socos repetidos; o Eliot
   morre em ~100 passos de combate. Falta tática (esquiva, distância) ou uma arma; a vida do monstro
   ainda não foi localizada na RAM (`0x8C1F2954` e `0x8C1F3A68` são medidores que se recompõem).
3. **Custo de navegação:** a câmera muda por posição; a navegação automática (`assistido_ir.py`)
   resolve deslocamentos, mas achar objetivos ainda depende de olhar as capturas.

Nenhum bloqueio é defeito do port: as mesmas situações acontecem igualmente nos dois modos.

## 6. Dependências legadas observadas

| Dependência | Onde | Situação |
|---|---|---|
| VMU (Maple, cartão de memória) | boot ("VMU not found"), LOAD GAME ("A VMU is not inserted") | sem dispositivo, sem save/load. Etapa N4 (não antecipada) |
| Esperas declaradas | só as já declaradas avançaram: `8C064A2C` (13 243 episódios), thread 3 `8C158860`, render `8C173086` (27 episódios), boot | nenhuma espera nova nas áreas novas; maior janela sem espera 9,7 M pontos (limite 64 M) |
| Registradores sem modelo | os mesmos avisos do boot da área 1 (`005F80DC/E4/E8`, `005F688C`, `005F7818`, PDTRA) | nenhum registrador novo nas áreas novas |
| GD-ROM, Maple, AICA, PVR | iguais nos dois modos nas áreas novas | consumidores de tempo do legado continuam (N2c, N3, N5–N7 não antecipados) |

## 7. Riscos restantes

- Cobertura ainda parcial da campanha: 4 salas, 1 inimigo, 1 item; sem armas, sem chefes, sem
  troca de personagem (Dogs), sem as áreas internas (Hello Market, Cinemax...).
- Os roteiros por passo dependem do contador da espera `8C064A2C`; um mod que reescreva o laço
  principal tem de chamar `bs::mods::wait_end()` (já exigido pelo B10).
- Esperas com contador de timeout continuam fora do inventário (risco do N2b-2).
