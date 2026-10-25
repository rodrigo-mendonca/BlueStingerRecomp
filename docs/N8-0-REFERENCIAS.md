# N8.0 — referências e harness confiável

Especificação aprovada pelo mantenedor em 2026-10-24 (com o ajuste do SPC e da ordem estritos) e implementada em
2026-10-25: `tools/dev/n8_referencias.py` (`capturar`, `validar`, `conferir`) e os testes `tools/dev/test_n8_referencias.py`
(unittest, sem o jogo). Resultado da execução: seção 8.

Base: `tools/dev/n8_referencias.py`, `runtime/debug/event_trace.*`, `runtime/debug/clock_log.*`,
`runtime/cpu/sh4_intc.cpp`, `runtime/pvr/holly_intc.cpp`, `tools/dev/gravacoes.py`, artefatos da captura inválida
(guardada em `analysis/n8/descartada/`, com um `LEIA-ME.txt`).

## 1. O que o `BS_EVENT_TRACE` registra de fato

Formato (`event_trace_dump`): `seq  ciclos  tipo  a  b`. A lista fica na memória e é gravada no `atexit` (o headless
sai por `return` de `main`, então grava; uma queda ou um `TerminateProcess` perde o arquivo inteiro).

| Tipo | Onde nasce | a / b | Significado real |
|---|---|---|---|
| `V` | `holly_intc::update` quando `holly_due` traz o VBlank-In | nº do VBlank / — | o bit VBlank-In foi **aceso no ISTNRM** (enfileirado), não entregue |
| `I` | `sh4_intc::take` | INTEVT / SPC | IRQ **entregue** à CPU (tratador `VBR+0x600` chamado) |
| `T` | `sh4_threads` (troca) | thread de origem / de destino | troca de contexto |
| `N` | `sh4_threads` (criação) | índice / entrada | thread nova |
| `R` | `sh4_threads` (continuação) | destino | rotina adiada pela interrupção retomada |
| `J` | `sh4_threads` (longjmp) | destino / thread | salto entre threads |

**Lacunas concretas:**
1. **Base de tempo misturada na coluna `ciclos`:** `V` grava `now = time_now(c)` (no experimental é o instante lógico
   T); `I`/`T`/`N`/`R`/`J` gravam `c->cycles`. No estável as duas coincidem; no experimental, não. A coluna não serve
   para ordenar nem comparar entre modos, e some no N8.1. **Regra: o comparador ignora a coluna 2 sempre.**
2. **`I` só tem o nível, não a fonte:** INTEVT 0x320/0x360/0x3A0 diz o nível IRL (6/4/2). Não se sabe se a entrega
   atendeu VBlank-In, VBlank-Out, fim de render (bits 0–2) ou Maple (bit 12).
3. **Enfileiramentos invisíveis:** só o VBlank-In gera `V`. VBlank-Out, fim de render, fim de DMA do Maple e
   `holly_intc_raise` acendem bits sem registro.
4. **Fusão invisível:** um VBlank-In com o bit ainda aceso conta só em `g_vblankFused`, fora do trace.
5. **Mascaramento invisível:** um evento enfileirado e mascarado por `IML*`/`SR.IMASK` não deixa rastro, nem a hora em
   que é finalmente entregue.
6. **Reconhecimento pelo tratador invisível:** a escrita do tratador que limpa `ISTNRM` não é registrada.
   `BS_CLOCK_LOG` registra só as **leituras** do `ISTNRM` (quantidade e hash por VBlank).
7. **Sem passo de jogo:** a correlação é possível fora do jogo. O `a` de `V` é o número do VBlank, e `quadros.tsv` tem
   `vblank → passo`.
8. **Memória:** tudo fica num vetor até o fim. O piloto mede o tamanho (estimativa de alguns MB a dezenas de MB por caso).

**O que o trace já prova:** a sequência de entregas (`I`, nível) e de trocas de thread entre dois VBlank-In; o SPC de
cada entrega; e o determinismo (`test_ordem` já o usa assim). Isso é linha de base da decisão 1 ("ordem dos eventos")
**por nível e SPC**, não por fonte: não permite declarar a equivalência completa da ponte Holly/INTC (seção 6).

**`BS_CLOCK_LOG` como complemento** (já existe, barato, uma linha por VBlank): quantidade e hash das leituras de TMU,
SPG_STATUS, MDST (Maple), RTC, ISTNRM/EXT/ERR e da RAM de som. É a observação de **timers** que faltava (L-d do
relatório anterior), ao nível do valor lido por VBlank.

## 2. Enfileirado × entregue (ponte Holly/INTC)

| Pergunta | Respondível hoje? | Com quê |
|---|---|---|
| quantos VBlank-In foram enfileirados | sim | `V` |
| quantas IRQs foram entregues, de que nível, e em que SPC | sim | `I` |
| um VBlank-In ficou sem entrega antes do próximo | sim, por nível | `V` sem nenhum `I` 0x320 até o `V` seguinte |
| que fonte cada entrega atendeu | **não** | precisa de trace no produto |
| eventos de render/Maple/VBlank-Out enfileirados | **não** (só indiretamente, pelo hash do `ISTNRM` lido no clock log) | precisa de trace no produto |
| fusões e máscaras | **não** (contadores fora do trace) | precisa de trace no produto |

**Sem mudar o produto no N8.0**, a linha de base fica: `V`, `I` (nível, SPC), `T/N/R/J` e o `BS_CLOCK_LOG`. O resto
vai para o N8.1 (seção 6).

## 3. Matriz dos 16 casos: artefatos obrigatórios

Legenda: **O** = obrigatório e com conteúdo além do cabeçalho; **V** = pode ficar vazio (a rota não produz; mesmo assim
o arquivo tem de existir com o cabeçalho, quando o jogo escreve um); **≥n** = mínimo de linhas de dados. Os mínimos vêm
da captura inválida (rotas) e do `sessao.json`/`N2-PLANO` (sessões). O piloto confirma e o valor final fica na tabela do
harness.

| Caso (×2 modos) | estado | arquivos | quadros | música | efeitos | falas | filme | cartão | saves fim | eventos | relógios |
|---|---|---|---|---|---|---|---|---|---|---|---|
| passo_newgame_area1 | O contíguo 1..3430 | O | O | V | O | O | O (NEWGAME) | V | vazio | O | O |
| passo_cgate | O 1..7971 | O | O | O | O | O | O | V | vazio | O | O |
| passo_heliporto | O 1..13172 | O | O | O | O | O | O | V | vazio | O | O |
| passo_combate | O 1..11672 | O | O | O | O | O | O | V | vazio | O | O |
| passo_load_opcoes | O 1..1718 | O | O | V | O | V | V | V (LOAD sem cartão) | vazio | O | O |
| passo_load_slot1 | O 1..2237 | O | O | O | O | V | V | **O: ≥1 `load ok`, 0 `falha`** | = importado (g1) | O | O |
| sessao-…140145 | O 1..7915 | O | O | O | O | V | V | **O: `load ok` + `save ok`** | **= `saves_fim` do `sessao.json`** | O | O |
| sessao-…145006 | O 1..7496 | O | O | O (4 trocas) | O | V | V | **O: ≥8 `load ok` + `save ok`** | **= `saves_fim`** | O | O |

**Cartão inicial** (antes de rodar, por caso; gravar o hash **bruto** e o **normalizado** de `gravacoes.hashes`):
- rotas sem cartão: pasta vazia;
- `passo_load_slot1`: cartão vazio + importação de `G1` (sha256 `1644836c…` do `.000` de origem), conferida por
  `importar.txt` (rc 0) e pelo arquivo `card0/BLUESTIN.000.bsv` presente;
- sessões: `saves_inicio`, comparado com `sessao.json.origem.cartao` **no formato em que foi gravado**: a `…140145`
  usou o bruto (`110cbb…`) e a `…145006` o normalizado (`adecf1…`, `f43a00…`). Aceitar se bater em qualquer dos dois
  formatos e registrar qual foi.

**Configuração única:** o mesmo `exe_sha256` e `disc_manifest_sha256` nos 16; os mesmos `BS_*` fora os caminhos; o
mesmo `BS_RELOGIO_FIXO`; os argumentos de cada sessão vindos de `gv.com_tempo`. Caso com outro valor → recusado.

## 4. Testes unitários do próprio harness (sem o jogo)

Estilo do repositório: `unittest` da biblioteca padrão, como `tools/dev/legendas/test_legendas.py`. Arquivo proposto
`tools/dev/test_n8_referencias.py`, com fixtures sintéticas em `tempfile.mkdtemp()` (pastas de caso montadas pelo
teste, com poucas linhas). Exigência: separar `validar_caso(pasta, caso, esperado) -> [problemas]` e
`comparar(ref, nova, caso) -> [diferencas]` como funções puras. Hoje a validação está misturada no `capturar` e a
comparação no `conferir`.

| # | Teste | Fixture | Esperado |
|---|---|---|---|
| U1 | arquivo ausente | caso sem `musica.tsv` onde a matriz exige **O** | `validar_caso` acusa "musica ausente" |
| U2 | arquivo vazio | `efeitos.tsv` só com o cabeçalho, exigido **O** | acusa "efeitos vazio" |
| U3 | vazio permitido | `musica.tsv` com o cabeçalho, matriz **V** | sem problema |
| U4 | passos descontínuos | `estado.tsv` 1,2,3,5,…,fim | acusa "passo 4 ausente" |
| U5 | passos truncados | `estado.tsv` até fim−1 | acusa (hoje `len >= fim-1` aprova: é exatamente o defeito) |
| U6 | cartão inicial errado | `execucao.json.saves_inicio` com outro hash | acusa "cartão inicial" |
| U7 | formato do hash do cartão | origem bruta + inicial bruto igual; origem normalizada + inicial normalizado igual | as duas aceitas |
| U8 | LOAD não executado | `passo_load_slot1` com `cartao.tsv` só com o cabeçalho | acusa "LOAD esperado" |
| U9 | LOAD com falha | `cartao.tsv` com `load falha` | acusa |
| U10 | log incompleto | `eventos.tsv` sem nenhum `V` depois de N passos / `quadros.tsv` acabando antes do último passo | acusa "log incompleto" |
| U11 | hashes inconsistentes | `manifesto.exe_sha256` ≠ `execucao.json.exe_sha256` de um caso | acusa "configuração diferente" |
| U12 | ambiente diferente | um caso com `BS_RELOGIO_FIXO` diferente | acusa |
| U13 | diferença real | referência × nova com uma linha de `estado` diferente no passo 100 | `comparar` acusa estado, passo 100 |
| U14 | diferença de save | hashes do fim diferentes | acusa saves |
| U15 | ausência dos dois lados | ref e nova sem `musica.tsv` onde a matriz exige **O** | **acusa** (hoje passa: `continue`) |
| U16 | vazio dos dois lados | ref e nova com `efeitos.tsv` vazio, exigido **O** | **acusa** (hoje `[] == []`) |
| U17 | ordem de eventos | `I` 0x320 e `T` trocados entre dois `V` | acusa "eventos" |
| U18 | coluna de tempo ignorada | mesmas linhas com `ciclos` diferentes | igual |
| U19 | SPC diferente | `I` com o mesmo nível e outro SPC | **acusa** (sem entrada de exceção) |
| U19b | SPC com exceção | a mesma diferença listada (caso, VBlank, SPCs, justificativa) | aceita, e o relatório lista a exceção usada |
| U19c | exceção que não casa | entrada com outro SPC ou outro VBlank | acusa; uma exceção nunca vale por padrão |
| U17b | entrega antecipada/atrasada | o mesmo `I` movido para o intervalo anterior/seguinte | acusa, apontando o intervalo |
| U20 | caso a mais ou a menos | nova sem um caso do manifesto | acusa |

Exemplo mínimo (forma, não código final):

```python
class TestValidar(unittest.TestCase):
    def test_passos_descontinuos(self):
        p = caso_sintetico(estado=[1, 2, 3, 5], fim=5)
        self.assertIn("estado: passo 4 ausente", n8.validar_caso(p, CASO, ESPERADO["passo_cgate"]))

    def test_ausente_nos_dois_lados(self):
        r, x = caso_sintetico(sem=["musica"]), caso_sintetico(sem=["musica"])
        self.assertTrue(n8.comparar(r, x, CASO_MUSICA_OBRIGATORIA))
```

## 5. Diferenças permitidas e reprovação imediata (N8.1, contra estas referências)

Comparação no **modo N8.1**: o executável sem ciclos (lógico) contra a referência **experimental**. A referência
**nativo** (estável por ciclos) continua sendo o alvo da compatibilidade com as gravações antigas, pela lista fechada
(`config/n2_excecoes.toml`), enquanto `--tempo nativo` não mudar.

**Ajuste aprovado pelo mantenedor (2026-10-24): o SPC e a ordem dentro do VBlank NÃO são relaxados globalmente.**

**Permitidas** (cada uma com a regra que a limita):
- coluna de tempo de todos os logs (`ciclos`, `vblank`, `tick`, ms): é unidade, não comportamento;
- os passos da lista fechada (1 passo por LOAD, `d11-carga-cartao`), só no confronto com a referência **nativo**;
- o desempenho (medido à parte: limite de 5%, B9).

**SPC: comparado sempre, estrito.**
- `eventos.tsv` guarda o SPC de cada `I`; o comparador confere a tupla `(nível, SPC)` na ordem.
- O `rte` voltar ao SPC (`ret_expect`) é invariante de execução, **não** prova de equivalência.
- Uma diferença de SPC só é aceita se estiver numa **lista de exceções explícita** (arquivo versionado, como
  `config/n2_excecoes.toml`): caso, VBlank, SPC da referência e o novo, a justificativa e a evidência de que é
  inofensiva. A evidência mínima:
  1. estado por passo e saves iguais;
  2. a sequência de eventos seguinte igual;
  3. os dois SPC na mesma função, sem acesso a memória compartilhada com o tratador entre eles, mostrado pelo
     disassembly.

  Sem a entrada, reprova. A lista é por diferença, nunca por padrão nem por faixa.
- No N8.0, a reprodutibilidade (o mesmo executável e o mesmo modo duas vezes) exige SPC **idêntico**, sem exceção.

**Ordem dentro do VBlank: comparada, estrita.**
- O comparador agrupa `eventos.tsv` por intervalo entre dois `V` e compara a **sequência ordenada** de cada intervalo:
  `I(nível, SPC)`, `T(de, para)`, `N(índice, entrada)`, `R(destino)`, `J(destino, thread)`.
- Detecta inversão (I↔T), entrega antecipada (o `I` muda para o intervalo anterior) e atrasada (para o seguinte),
  apontando o primeiro intervalo e a posição divergentes.
- **Deslocamento não tem exceção no comparador do N8.0:** inversão, entrega antecipada ou atrasada, evento a mais ou a
  menos reprovam sempre. A lista de exceções só cobre um `I` na **mesma posição**, com o mesmo nível e outro SPC, e
  cada entrada libera **uma** diferença (é consumida ao casar). Se o N8.1 precisar de exceção de posição, ela exige
  campos explícitos (posição na referência e na nova) e correspondência exata, com a aprovação do mantenedor antes.
- **Limite da instrumentação atual:**
  - `relogios.tsv` (`BS_CLOCK_LOG`) e `cartao.tsv` não ficam no mesmo fluxo ordenado do `eventos.tsv`. O primeiro é
    resumido por VBlank (quantidade e hash, na ordem de leitura); o segundo é por passo.
  - Por isso a ordem **entre** uma leitura de relógio ou um LOAD e uma interrupção no mesmo VBlank **não é
    observável** no N8.0.
  - O que se compara: cada fluxo na sua ordem (o hash do VBlank cobre a ordem das leituras de relógio), alinhado por
    VBlank ou por passo.
  - A intercalação vai para a instrumentação do N8.1 (seção 6).

**Reprovação imediata:**
- `estado` diferente em qualquer passo (fora da lista fechada, só contra a nativo);
- qualquer save do fim diferente, ou cartão inicial diferente;
- `arquivos`: ordem ou conteúdo diferentes;
- `quadros` por passo diferentes;
- música, efeitos, falas, filme ou cartão: sequência diferente (sem a coluna de tempo);
- `eventos`: em qualquer intervalo entre dois `V`, sequência ordenada `I(nível, SPC)/T/N/R/J` diferente (inversão,
  entrega antecipada ou atrasada, SPC diferente), ou um `V` a mais ou a menos, sem entrada justificada na lista de
  exceções;
- `relogios` (`BS_CLOCK_LOG`): quantidade ou hash de leituras de TMU, SPG, MDST, RTC ou IST diferentes em algum VBlank
  (no experimental o TMU já é o instante lógico, D7, então tem de ficar igual);
- artefato obrigatório ausente ou vazio;
- código de saída ≠ 0; uma linha `erro:` na saída do jogo (no experimental a guarda de falta de progresso é fatal e
  sai assim); um deadlock contado (`deadlock` ≥ 1); `interrupcao nunca reconhecida`. Os contadores normais do
  `[progresso]` (`deadlock 0`, e no `--tempo nativo` as "faltas de progresso N" do modo sombra, que é só diagnóstico,
  1–2 durante o filme nativo) não reprovam;
- duas execuções do mesmo executável diferentes (determinismo).

## 6. Lacunas que o N8.0 não resolve sem mudar o produto → testes do N8.1

| Lacuna | Por que o N8.0 não fecha | Teste complementar no N8.1 |
|---|---|---|
| fonte de cada IRQ (bits do ISTNRM atendidos) | precisa de um tipo novo no trace (`H`: bits acesos por `update`/`raise`; `A`: bits limpos pelo tratador) | **N8.1 passo 0**: só os tipos novos, sem mudar comportamento. Prova de que o build é o mesmo comportamento: `conferir` contra as 16 referências do N8.0 (todos os artefatos iguais). Só então a nova linha de base `H/A` é capturada com esse build, e as mudanças da cadência são conferidas contra ela |
| fusões e mascaramento | idem (contadores fora do trace) | idem: `H` com o estado das máscaras no momento |
| intercalação de leituras de relógio, LOAD e IRQ no mesmo VBlank | `BS_CLOCK_LOG` é agregado por VBlank e o cartão é por passo | tipos no mesmo fluxo do `BS_EVENT_TRACE` (p.ex. `C`: leitura de TMU/IST com o valor; `L`: operação do cartão), também no passo 0 |
| equivalência completa da ponte Holly/INTC | sem `H`/`A`, a fonte de cada entrega não é observável | **não declarar** equivalência da ponte com os dados do N8.0; só por nível e SPC |
| medidores do CRI (TCNT0 lido direto) | o `BS_CLOCK_LOG` resume por VBlank, sem dizer qual função leu | trace por PC das leituras de TCNT0 (diagnóstico) antes e depois dos slots |
| eventos dentro de um filme | o passo não anda durante o filme | comparar `eventos`/`relogios` por VBlank no trecho do filme (o NEWGAME já está nas referências) |
| estado fora da janela de 128 KB | o hash cobre só as variáveis do jogo | hash por passo das estruturas de thread/tarefa do SDK (trace novo, diagnóstico) |

## 7. Ordem de execução (cumprida em 2026-10-25)

1. Harness: `validar_caso`/`comparar` puros, matriz da seção 3, cartão inicial (bruto e normalizado), `eventos.tsv`
   (`BS_EVENT_TRACE`) e `relogios.tsv` (`BS_CLOCK_LOG`) nos `LOGS`, recusa de ausente/vazio, coluna de tempo ignorada
   nos eventos, configuração única; os testes U1–U20 (unittest, sem o jogo).
2. Piloto: `passo_load_slot1_experimental` (curto, LOAD, música, cartão), capturado com o executável da Preview
   corrigida, numa pasta `analysis/n8/piloto/`. Depois `conferir` estrito do piloto contra ele mesmo (0 diferenças) e o
   negativo `--ambiente BS_RAM_MUTACAO=3000:8C24FBCC` (tem de acusar `estado`). Medir o tamanho de `eventos.tsv`.
3. Só com o piloto aprovado: os 16 casos.

## 8. Resultado (2026-10-25)

**Executável único:** a Preview gerada depois das correções SPIR-V e do ícone (`dist\BlueStinger-Preview`, `make_preview`
pelo Builder), `exe_sha256 d05b9014…5b38`, dados `disc.manifest 24fa3cb0…8524`, relógio `1700000000`. O manifesto
registra o commit `5f2111e-dirty`: as correções ainda não estavam no commit; o que identifica a referência é o SHA do
executável. Pacote em `analysis/n8/referencias/` (local, fora do git), ~254 MB, dos quais 113 MB de `eventos.tsv`.

**Piloto** (`analysis/n8/piloto`, `passo_load_slot1_experimental`): 2237 de 2237 passos; `conferir` contra ele mesmo
com 0 diferenças; o negativo `BS_RAM_MUTACAO=3000:8C24FBCC` acusou `estado` a partir do passo 1496; `eventos.tsv`
1,84 MB. A primeira validação do piloto reprovou por um defeito do harness (a vivacidade casava os contadores normais
`deadlock 0`); corrigido para a regra da seção 5 e coberto por teste com as linhas reais da saída. O manifesto do
piloto é anterior à marca `parcial` (achado 1 da revisão abaixo): o `validar` atual o recusa como captura completa
incompleta, como deve; o caso em si continua válido. O piloto é evidência do método, não referência.

**16 casos:** capturados em 2 668 s, todos válidos (rc 0, `estado` exatamente 1..N com N ≥ fim, artefatos obrigatórios
com dados, logs até o fim, cartão inicial e do fim conforme a seção 3, configuração única).

| Caso | passos (fim) | `I` entregues | cartão |
|---|---|---|---|
| passo_newgame_area1 (nativo / experimental) | 3 430 | 151 465 / 151 578 | — |
| passo_cgate | 7 971 | 188 253 / 188 375 | — |
| passo_heliporto | 13 172 | 229 943 / 230 088 | — |
| passo_combate | 11 672 | 217 910 / 218 043 | — |
| passo_load_opcoes | 1 718 | 25 641 / 25 729 | — (LOAD sem cartão) |
| passo_load_slot1 | 2 237 | 29 800 / 29 893 | 3 `load ok` |
| sessao-20261021-140145 | 7 915 | 97 610 / 65 967 | 8 `load ok`, 1 `save ok` |
| sessao-20261021-145006 | 7 496 | 102 026 / 69 041 | 12 `load ok`, 1 `save ok` |

**Reprodutibilidade** (o mesmo executável): `conferir` estrito de um caso por família, nos dois modos
(`passo_newgame_area1_experimental`, `passo_load_opcoes_nativo`, `passo_load_slot1_nativo`,
`sessao-20261021-145006_experimental`): **0 diferenças** em todos os artefatos, inclusive `eventos` (SPC e ordem por
VBlank) e `relogios`. **Negativo** com `BS_RAM_MUTACAO=3000:8C24FBCC`: acusado nos dois casos (`passo_load_opcoes_nativo`:
estado a partir do passo 1481; a sessão `145006`: save `BLUESTIN.002` do fim, estado a partir do passo 1492 e quadros).

**Cobertura que as referências não dão** (registrada, não presumida):
- música: `passo_newgame_area1` e `passo_load_opcoes` não têm vozes (a matriz marca V); legendas: nenhuma em
  `passo_load_opcoes` e `passo_load_slot1`;
- SAVE: só nas duas sessões;
- filmes: só o NEWGAME; os outros 16 IDs seguem no `n5_e001` e na matriz do N5;
- fonte de cada IRQ, fusões, mascaramento e a intercalação com leituras de relógio e o cartão: seção 6 (N8.1 passo 0).

**Revisão Codex** (`reviews/codex-20261025-081520.md`, 6 achados, todos aceitos):
1. (alta) uma captura completa podia omitir sessões ausentes: conjunto canônico dos 16 (`CASOS_N8`), a captura sem
   `--casos` recusa se faltar algum, o manifesto marca `parcial`, e `validar` exige os 16 num manifesto não parcial;
2. (média) o estado contíguo aceitava passos fora de ordem e passo 0: agora exatamente 1..N na ordem;
3. (média) uma exceção de SPC podia liberar várias diferenças iguais: cada entrada é consumida ao casar;
4. (média) as guardas das suítes aceitavam artefatos vazios ou truncados iguais: `Same` exige linhas de dados e
   `Completa` exige dados em todo log do job e quadros, traço e estado no último índice do roteiro (`test_controles`
   e `test_pd_nativo`); a prova offline (44 execuções aprovadas aceitas, 15 negativos rejeitados) achou ainda dois
   defeitos da própria correção, corrigidos antes de qualquer execução: um `-1` literal passado como texto pelo
   PowerShell e o roteiro ausente que deixava o último índice em 0. Execução real com as guardas finais:
   `test_controles -Casos K11,K13` 12/12 e `test_pd_nativo -Casos P3` 32/32;
5. (baixa) a documentação permitia exceção de posição que o comparador não implementa: fica a regra estrita
   (seção 5);
6. (baixa) esta seção e o andamento do plano.

Testes do harness: 41 (unittest, sem o jogo), todos aprovados; as 16 referências continuam válidas com as regras
endurecidas (`validar`).
