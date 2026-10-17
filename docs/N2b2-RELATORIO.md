# N2b-2 experimental: tempo lógico sem ciclos estimados (relatório)

Data: 2026-10-17. Base: `246e6b3` mais a investigação P0/P1 (sem commit). Autorização do
mantenedor: N2b-2 **só em modo experimental, atrás de uma opção explícita**. O padrão continua o
tempo nativo estável. Sem N2c, sem N3–N7, sem mudança no escalonador do gameplay. **Sem commit.**

Uso: `bluestinger --tempo experimental` (também com `--headless`, `--arquivos legado` e no build
com mods). Teste: `tools/dist/test_n2b2.ps1`.

## 1. O que muda

| | Estável (`--tempo nativo`, padrão) | Experimental (`--tempo experimental`) |
|---|---|---|
| Instante lógico | o contador de ciclos estimados (`BS_CYCLES`) | um instante próprio, T, que os ciclos não movem |
| Quando o tempo anda | a cada instrução, pelo custo estimado | **só** quando o contexto corrente chega a um ponto de espera declarado, e só até o próximo evento real |
| TMU, SPG_STATUS, MDST, RTC, ISTNRM | leem os ciclos | leem T (D7: o TMU fica parado dentro do passo) |
| Thread 3 (servidor do CRI) | gira até ser preemptada | bloqueia depois de uma passada ociosa e acorda no próximo evento |
| Falta de progresso | aviso (modo sombra) | erro com threads, PCs e últimas esperas |
| Espera sem evento futuro | — | erro de deadlock com a espera e a thread |
| Espera síncrona que dá volta | — | erro explícito |

O componente nativo **não escolhe threads**: a troca continua sendo a do despachante do SDK, pelo
`rte` recompilado. Ele só faz o tempo andar e entrega os eventos vencidos como interrupções.

**Não há avanço silencioso:**
- o tempo nunca anda pela guarda de progresso;
- nunca anda por ciclos;
- nunca anda por espera artificial.

Se nenhum ponto de espera é alcançado, a execução para com erro.

### 1.1 Peças

| Peça | Arquivo |
|---|---|
| Provedor experimental (o `NativeTime` com `exp_`): instante T, espera, alvo, guarda, deadlock | `transitional/time_adapter.cpp` |
| Tabela das esperas e do que libera cada uma (`kWaitDecls`) | idem |
| Ponto de espera com contexto (`TimeProvider::wait`) e entrega forçada no poll seguinte | `runtime/cpu/time_hooks.h`, `runtime/cpu/sh4_runtime.cpp` |
| Evento vencido no mesmo instante (`holly_pending`) | `runtime/pvr/holly_intc.cpp` |
| Próxima troca de linha do feixe (alvo das esperas do SPG) | `runtime/pvr/pvr_regs.cpp` (`pvr_spg_next_line_change`) |
| Ociosidade da thread 3, com RAM rastreada pelo sistema | `transitional/cri_idle.cpp` |
| Contadores de efeito fora da RAM (hardware, store queue, host) | `runtime/memory/effect_counters.h` |
| Esperas declaradas (12 novas) | `config/bluestinger_us.toml`, `[native] esperas` |
| Opção de linha de comando e RAM rastreada | `app/main_sdl.cpp`, `app/game_host.cpp` |

### 1.2 Pontos de espera e condição de liberação

| Ponto (back-edge) | Tipo | Libera |
|---|---|---|
| `8C064A2C` | Evento | VBlank do jogo (bandeira `0x8C24F88A`) |
| `8C173086` | Evento | fim do render anterior; timeout de 166 ms pelo TMU (T) |
| `8C0978F6` | Evento | bit com timeout pelo TMU (não exercitado na rota) |
| `8C172628`, `8C17262E` | LinhaSpg | vsync do SPG acender e apagar (boot, V2) |
| `8C172808` | Evento | VBlank-In em `ISTNRM` (boot, V2) |
| `8C172EF8` | Evento | contador do callback de VBlank `8C172602` (boot, V13) |
| `8C1C6D06`, `8C1C6D0E` | LinhaSpg | linha do SPG sair de 0 e voltar a 0 (boot, V13) |
| `8C1C1DC6` | Síncrona | DMA ch2: `C2DST` = 0 (conclui na escrita) |
| `8C096EE8`, `8C096EF8`, `8C097254`, `8C097264` | Síncrona | FIFO do G2 vazia (`SB_FFST`; conclui na escrita) |
| `8C158860` | OciosaCri | thread 3: a passada desde a visita anterior foi ociosa |

Em cada tipo, o que acontece quando o laço volta ao ponto (a condição ainda é falsa):
- **Evento:** T vai ao próximo evento das filas (VBlank-In/Out, fim de render, Maple, GD-ROM). Se
  houver evento vencido ainda não entregue, ele é entregue sem andar o tempo.
- **LinhaSpg:** T vai à próxima troca de linha do feixe ou ao próximo evento, o que vier antes.
  `SPG_STATUS` (linha, vsync, campo) só muda nesses instantes.
- **Síncrona:** erro.
- **OciosaCri:** se a passada foi ociosa, como Evento; se não, a próxima passada roda já.

No tempo estável os 12 pontos novos são ignorados (seção 4.1).

### 1.3 Ociosidade da thread 3 (critério validado, forma de produto)

É o critério `pular6` da investigação (`docs/N2b-PROPOSTA.md`, 9.2). A passada é ociosa quando
nada disto aconteceu nela:
- evento (interrupção aceita, troca de thread, VBlank, evento do Holly, bloco de GD-ROM);
- mudança de valor na RAM principal, fora da pilha morta da thread 3, dos medidores do TMU do CRI
  e do índice do rodízio;
- escrita de hardware (MMIO, VRAM, AICA);
- descarga de store queue;
- escrita do host;
- mudança no estado do GD-ROM ou do TMU.

**No produto**, a RAM é alocada com o rastreamento de escrita do Windows (`MEM_WRITE_WATCH`):
- a cada visita, só as páginas escritas desde a anterior são comparadas com uma cópia;
- não há custo nas escritas da CPU;
- o rastreamento vê também as escritas do runtime e do host, o que o torna mais conservador que o
  detector de investigação.

Sem esse recurso do sistema, o modo experimental não inicia: não há alternativa silenciosa.

## 2. Dependências de ciclos eliminadas

| Consumidor | Estável | Experimental |
|---|---|---|
| `time_now` (TMU, `SPG_STATUS`, `MDST`, RTC, `ISTNRM`, fim de render e de Maple, transferência do GD-ROM legado) | ciclos | T |
| Instante de entrega dos eventos (VBlank-In/Out, render, Maple, GD-ROM) | quando os ciclos passam do devido | no ponto de espera que leva T até o evento |
| Alternância das threads 0 e 3 | preempção depois de N ciclos de trabalho da thread 3 | bloqueio da thread 3 depois de passada ociosa; o evento a acorda |
| Esperas temporais do boot (SPG, `ISTNRM`, callback de VBlank) | o laço gira até os ciclos cruzarem a linha | declaradas, com alvo exato |
| Timeouts pelo TMU (render, bit) | ciclos | T, que anda de evento em evento |
| Duração das cargas e do trabalho longo (D11) | VBlanks de CPU | zero: o trabalho não consome tempo lógico |

**O que ainda usa o contador de ciclos no experimental** (e não move o tempo):
- a cadência do poll, que verifica interrupções pendentes e conta trabalho para a guarda;
- os diagnósticos.

Uma interrupção já pendente e desmascarada pode ser atendida num poll um pouco antes ou depois, o
que muda só a instrução interrompida, de forma determinística (N10).

## 3. Resultados (`tools/dist/test_n2b2.ps1`, 17 casos, 0 falhas)

Roteiros NEW GAME com relógio fixo: exploração (16 000 VBlanks) e área 1 (8 000).

| # | Critério (plano: B) | Resultado |
|---|---|---|
| N1 | Progresso | exploração completa; 24 597 bloqueios da thread 3; 56 794 avanços do tempo (791 por linha do SPG) |
| N2 | Leituras (B3) | **mesma sequência de 7 915 leituras** (arquivo, deslocamento, tamanho, SHA-256) |
| N3 | Quadros (B6) | **mesma sequência de 2 517 trocas de quadro** do boot até o gameplay (título, menus, os dois vídeos, as cargas), com o mesmo roteiro; só o quadro intermediário do boot (`F1211C1A…`, V4–V13 do estável) fica de fora, e o teste exige exatamente essa exceção |
| N3b | Gameplay (B6) | com as entradas alinhadas por passo de jogo (4.5.1), cada troca de quadro comparada no mesmo ponto lógico (leitura do controle alinhada), da área 1 até o último ponto comum (leitura 15 953): **5 530 trocas iguais**; a única exceção é o menu de itens, entre o START que o abre (leitura 5974) e o que o fecha (6174), com 99 trocas de cada lado, 86 delas diferentes (D11) |
| N3c | Gameplay (B3) | com as entradas alinhadas, as mesmas leituras |
| N4 | Áudio | **mesma sequência de escritas da CPU na AICA na rota inteira**, sem as cópias de guarda do anel (seção 4.3): exploração (1 023 002, com o roteiro original e com o alinhado) e área 1 |
| N5 | Eventos (B1, B2) | 99,94% dos VBlanks do experimental têm um padrão de eventos que existe no estável; 15 padrões contra 31 |
| N6 | Provedores (B4, D9a) | no experimental, legado = nativo **VBlank a VBlank** (quadros e áudio) até o gameplay, e as leituras têm o mesmo conteúdo |
| N7 | Timeout (B5) | render perdido sai pelo timeout (55,2 M ticks; estável 54,4 M), sem travar |
| N8 | Liveness (B7) | espera do VBlank não declarada → erro com thread 0 e `8C064A2C`; thread 3 que nunca bloqueia → erro com a thread 3 e a espera `8C158860`; `SB_FFST` ocupado → erro da espera síncrona `8C097254` (o estável com a mesma mutação roda) |
| N9 | Detector da thread 3 | bloquear depois de toda passada muda leituras e quadros: o detector é necessário |
| N10 | Determinismo (B8) | duas execuções idênticas (leituras, quadros, eventos, tempo, relógio, estado) |
| N11 | Estável inalterado | `--tempo nativo` = executável anterior ao N2b-2, **byte a byte** (trace com ciclos, leituras, relógio, estado, tempo, quadro) |
| N12 | Desempenho (B9) | experimental 15,63 ms por campo, estável 17,95 ms (−13%; seção 4.6) |
| N13 | Mods (B10) | o build com mods dá as mesmas leituras e quadros |

**SKIP declarados:**
- a espera síncrona do DMA ch2 (`8C1C1DC6`) não é alcançada na rota, então o negativo
  `c2dst_ocupado` é vazio;
- o negativo `cri_ignora_ram` não muda nada observável na rota (seção 5).

**B11 (TMU, D7):** os medidores do CRI gravam durações 0 no experimental (TMU parado no passo). A
lógica não depende deles: N2–N4 mostram as mesmas leituras, quadros e áudio. O timeout de render,
que é o único consumidor do TMU que decide, continua funcionando (N7).

Unitário novo: `pvr_spg_next_line_change_is_exact`. Ele falha com a primeira implementação
(arredondamento no limite de linha, seção 4.4) e passa com a corrigida.

## 4. Diferenças em relação ao modo estável (justificadas)

### 4.1 O modo padrão não muda
No estável, os 12 pontos de espera novos e os contadores de efeito não mudam nada (N11, byte a
byte). Custo medido: seção 4.6.

### 4.2 Linha do tempo: o trabalho não consome VBlanks (D11)

| Ponto | Estável | Experimental | Diferença |
|---|---|---|---|
| Cargas do boot | V13–V33 | V2–V3 | — |
| Título | V226 | V209 | 17 |
| Início do vídeo `NEWGAME.SFD` | V2096 | V2079 | 17 |
| Carga depois do vídeo | V2256–V2264 | V2229 | 27 a 34 |
| Início do vídeo `E001.SFD` | V4480 | V4446 | 34 |
| Área 1 | V4632–V4643 | V4596–V4601 | 36 a 42 |

Causas:
- **cargas e boot instantâneos:** o estável gasta de 9 a 33 VBlanks de CPU;
- **partida dos vídeos mais curta:** no estável, cerca de 10 VBlanks de decodificação e enchimento
  de buffer.

O conteúdo e a ordem são os mesmos (N2–N4). O roteiro de entrada é indexado pelas leituras do
controle, e o estável lê o controle 26 vezes a mais durante as cargas. Com o mesmo roteiro, os
botões do gameplay caem em outros passos do jogo, e a partir daí os quadros divergem: o N3 termina
em V5827 do estável, depois dos botões. Com as entradas alinhadas, o gameplay coincide (4.5.1).

### 4.3 Áudio
A única diferença está nas **cópias de guarda** do anel do stream de vídeo:
- são 8 palavras depois do fim de cada canal (`009FBF80`, `009FFFC0`), que repetem o início do
  anel;
- o escritor do stream as regrava a cada VBlank enquanto espera os dados;
- com a partida do vídeo mais curta, são 4 regravações a menos em cada vídeo.

Todas as outras escritas (driver de som, comandos, dados dos streams) são idênticas e na mesma
ordem.

### 4.4 Quadros
- O estável mostra um quadro intermediário do boot (V4–V13, estado parcial durante as cargas). O
  experimental não chega a mostrá-lo.
- O experimental não perde quadros: 4 007 renders contra 3 986 em 8 000 VBlanks. O estável perde
  renders nos campos mais pesados.

### 4.5 Estado no mesmo passo de jogo
Comparação da RAM no estável em V5600 e no experimental em V5558 (o mesmo passo, antes do
primeiro botão): **194 palavras diferentes em 16 MB, nenhuma na região de variáveis do jogo**
(`0x8C240000`–`0x8C25FFFF`).

| Grupo | O que é |
|---|---|
| carimbos e medidores do TMU (`0x8C1DD1C8`…, `0x8C2DA480`, `0x8C2CF908`) | congelados no passo (D7); durações 0 |
| contadores de VBlank e de passadas do CRI (`0x8C2B4488`…, `0x8C2CFBA4`…, `0x8C1DD5B4`) | menos VBlanks e passadas decorridos (4.2) |
| pilha e estado da thread 3 (`0x8C2D59C0`–`0x8C2D5D2C`, `0x8C1DD1BC`) | no estável, a thread 3 foi preemptada no meio de uma passada; no experimental, está bloqueada no topo do laço |
| índice do rodízio dos streams (`0x8C2DA97C`) | anda a cada passada (exclusão já conhecida) |

### 4.5.1 Gameplay: a divergência era da entrada, não da lógica

A revisão do Codex pediu localizar o primeiro estado divergente. A bissecção da RAM a cada 2
VBlanks em torno do botão X mostrou:
- o estado do jogo é **idêntico** até o botão;
- a reação ao X (`0x8C24C5D0` = `0x400` e um contador decrescente em `0x8C24C5EC`) aparece no
  experimental 8 VBlanks depois do aperto, e no estável 24 VBlanks depois.

**Causa:** o índice do roteiro de entrada é o **número de leituras do controle** (`GetCond`), não
o VBlank. O jogo lê o controle a cada VBlank, também durante as cargas, e no estável as cargas
duram mais VBlanks. Medido pela coluna "entrada" do `BS_FRAME_LOG` nos quadros alinhados: no mesmo
passo de jogo da área 1, o estável fez **26 leituras a mais**.

Com o roteiro deslocado em −26 leituras (`tools/dist/roteiros/newgame_exploracao_n2b2.txt`), o
jogador aperta os mesmos botões nos mesmos passos do jogo. O resultado na rota inteira de
exploração (16 000 VBlanks):
- **quadros:** comparados em cada ponto lógico (leitura do controle alinhada), são iguais da
  área 1 ao fim. A única exceção é o menu de itens, entre o START que o abre e o que o fecha
  (leituras 5974–6174, V6049–V6219 do estável): o menu carrega modelos (janela D11), e são 86
  trocas diferentes de cada lado (N3b);
- **leituras:** as mesmas (N3c);
- **áudio:** as mesmas 1 023 002 escritas (N4).

### 4.6 Desempenho
Medida sequencial e alternada, 3 000 VBlanks, cinco rodadas (trabalho médio por campo):

| Executável e modo | Média | Rodadas |
|---|---|---|
| anterior ao N2b-2, estável | 18,82 ms | 20,11 · 19,25 · 18,65 · 18,06 · 18,04 |
| novo, estável | 19,19 ms (+2,0%) | 20,97 · 19,26 · 19,04 · 18,43 · 18,27 |
| novo, experimental | 16,58 ms (−12%) | 16,91 · 16,41 · 18,11 · 15,75 · 15,72 |

- **Experimental:** mais rápido, porque a thread 3 deixa de girar sem efeito.
- **Estável:** +2,0% de média (de 0% a +4% por rodada pareada), dentro do limite do B9 (5%), mas é
  um custo no modo padrão. Candidatos, não isolados:
  - o incremento do contador de escrita de hardware em todo `hwWrite`, inclusive nas escritas na
    VRAM;
  - a chamada ao ponto de espera da thread 3 a cada passada (2,67 M por 8 000 VBlanks, ignorada
    no estável);
  - o layout do código regenerado.
- **Decisão (2026-10-17):** o mantenedor aceitou os +2% como custo conhecido, dentro da
  tolerância do B9. Não serão investigados nem otimizados agora.

### 4.7 Defeito encontrado e corrigido durante a implementação
Na primeira execução, a espera "linha volta a 0" (`8C1C6D0E`) andava um campo inteiro por volta e
nunca saía:
- `pvr_spg_next_crossing` trunca o início de linha;
- no instante exato de um VBlank, `pvr_spg_position` ainda dava a linha anterior;
- a "próxima travessia" caía no campo seguinte.

`pvr_spg_next_line_change` foi reescrita com a mesma conta de `pvr_spg_position` (teto). O
unitário pega o defeito.

## 5. Riscos restantes

| Risco | Situação |
|---|---|
| Cobertura de um roteiro só (NEW GAME até a área 1, dois vídeos, ADX, menus) | Troca de sala, combate, save/load e finais não exercitados (U7). Uma espera não declarada nessas rotas vira erro com PC: falha explícita, não silenciosa |
| Esperas que mudam estado a cada volta (contador de timeout) | Fora do inventário (`docs/N2b-ESPERAS.md`); no experimental, sairiam pelo timeout por outro caminho |
| Gameplay | Explicado (4.5.1): com as entradas alinhadas por passo de jogo, quadros, leituras e áudio iguais na rota inteira, salvo a transição do menu de itens (D11). A equivalência vale só para a rota coberta: andar, botões isolados, menu de itens, ficar parado |
| Roteiros de entrada indexados por leitura do controle | Um roteiro gravado no estável cai em outros passos do jogo no experimental, porque as cargas leem o controle menos vezes. Comparações de gameplay têm de alinhar as entradas por passo (como em N3b) |
| `cri_ignora_ram` sem diferença observável | O componente de RAM do detector não foi provado necessário nesta rota (é conservador) |
| `8C1C1DC6` (DMA ch2) não alcançado | O caminho de erro das esperas síncronas está provado pelos laços de `SB_FFST` |
| D11: cargas instantâneas | Mudança de comportamento visível (telas de carga mais curtas, menos quadros perdidos). Fica atrás da opção, documentada; não é avanço artificial |
| Rastreamento de escrita só no Windows | Sem ele o modo não inicia (erro claro) |
| Modo com janela | Mesmo caminho do headless (as mesmas opções no `GameHost`); não foi executado com janela nesta etapa |

## 5.1 Regressão

**Regressão completa** (2026-10-17, 03:36–05:35, sequencial):

| Etapa | Resultado |
|---|---|
| Builds release, debug, mods e investigação | ok |
| Unitários | 212 |
| ctest, sem e com o disco | 100% |
| `make_builder`, `make_preview` | ok |
| `test_preview` | 9/0 |
| `test_gameplay` | 4/0 |
| `test_builder` | 18/0 |
| `test_projeto` | 13/0 |
| `test_arquivos` | 6/0 |
| `test_ordem` | 8/0 |
| `test_tempo` | 35/0 |
| `test_n2b` | 44/0 |
| `test_cri` | 33/0 |
| `test_efeito` | 17/0 |
| `test_n2b2` | 17/0, com 2 SKIP declarados |

Na regressão, o desempenho deu 17,23 ms por campo no estável e 14,75 ms no experimental.

A primeira tentativa parou no `n0_fechamento`, a catraca do `bs-hwscan`: `sh4_wait_point` fazia
`next_event = cycles`, uma leitura direta de `c->cycles` em código novo do runtime. A correção usa
`next_event = 0`, o mesmo idioma do fim do DMA do canal 2, com o mesmo efeito.

## 6. Rollback
- O padrão não mudou: `--tempo nativo`.
- Backups:
  - `analysis/rollback-antes-n2b2/` tem o código gerado e os executáveis release, release com mods
    e debug anteriores, com hashes;
  - `dist/BlueStinger-Preview.p0p1-246e6b3` tem a Preview anterior.
- Os backups de Preview anteriores seguem intactos.

## 7. Revisão do Codex

**`reviews/codex-20261017-015745.md`:** todos os achados aceitos.

| Achado | Ação |
|---|---|
| [MÉDIA] A equivalência de gameplay não foi demonstrada; a divergência depois do primeiro botão não estava explicada | Bissecção da RAM: o estado é igual até o botão. A causa é o índice do roteiro, que conta leituras do controle (26 a mais no estável, por causa das cargas). Com as entradas alinhadas, quadros, leituras e áudio iguais na rota inteira, salvo a transição do menu (N3b, N3c, N4) |
| [MÉDIA] N3 aceitava vários quadros exclusivos no boot e não exigia o término | Exige o término e exatamente o quadro de boot documentado (hash e faixa de VBlank) |
| [MÉDIA] N4 calculava a igualdade da rota inteira, mas não a afirmava | A igualdade da rota inteira é o critério, nos dois roteiros, com o término exigido |
| [MÉDIA] N11 passava com artefatos ausentes | Exige o término das duas execuções e os seis artefatos presentes e não vazios; o texto diz "artefatos selecionados" |
| [BAIXA] N6 comparava o áudio por ponto de controle | Escrita a escrita, com o VBlank (`BS_AICA_LOG_TUDO`) |
| [BAIXA] O limite de "evento vencido não entregue" acumulava ocorrências não consecutivas | Zerado a cada entrega de evento |
| [BAIXA] O unitário não cobria a fase nem o entrelaçado | Casos com fase do BIOS (linha 123 e 524, segundo campo) e `SPG_CONTROL.interlace`; confere que o `SPG_STATUS` inteiro só muda no instante devolvido |

**`reviews/codex-20261017-024221.md`** (verificação): a explicação da divergência foi confirmada no
código (`Scripted::read` indexa por `GetCond`), assim como o −26 aplicado par a par e as correções
de N4, N11, do limite de evento vencido e do unitário. Quatro achados de rigor nos testes, todos
aceitos:

| Achado | Ação |
|---|---|
| [MÉDIA] N3b limitava a janela do menu só de um lado, o realinhamento podia pular quadros do experimental e o fim do experimental ficava de fora | Comparação em cada ponto lógico (leitura do controle alinhada), dos dois lados, até o último ponto comum; a exceção é limitada pelos dois STARTs do menu |
| [MÉDIA] N4 não comparava a área 1 e não exigia o término da execução alinhada | Áudio da área 1 (estável × experimental) na rota inteira; término exigido em todas as execuções |
| [BAIXA] N3 não conferia a faixa V4–V13 do quadro de boot | Conferida no log bruto, com os quadros vizinhos (V3 e V14) iguais aos dois primeiros do experimental |
| [BAIXA] N6 não exigia o término da execução nativa | Término e artefatos não vazios dos dois provedores |

Verificado sem problema pelo Codex na primeira rodada:
- os ciclos não movem o relógio no experimental;
- as filas do display e do disco entram no alvo;
- o poll forçado preserva o PC de retomada (SPC);
- `holly_pending`;
- os tipos das esperas, conferidos no binário, inclusive o `bra` com delay slot da thread 3;
- o detector reproduz o `pular6`;
- o modo estável não recebe estado do experimental. O vazamento é só de custo, e as fontes
  propostas para os +2% são plausíveis.
