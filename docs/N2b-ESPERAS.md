# N2b P0: inventário das esperas temporais (relatório)

Data: 2026-10-16. Proposta: `docs/N2b-PROPOSTA.md`, seção 9.1. Sem commit.

## Resultado

Na rota coberta, a CPU executa 833 laços distintos. **Seis** têm voltas sem progresso, e as
seis são esperas temporais:

| Laço (retomada) | Voltas | Sem progresso | Declarada? | Evento que libera |
|---|---|---|---|---|
| `8C064A26` | 3 595 537 699 | 3 595 526 772 | sim (`8C064A2C`) | VBlank |
| `8C17306A` | 238 937 | 204 357 | sim (`8C173086`) | fim do render (timeout de 166 ms) |
| `8C172804` | 474 534 | 474 533 | **não** | VBlank-In (`ISTNRM` bit 3) |
| `8C172EF2` | 361 951 | 361 950 | **não** | VBlank (callback `8C172602` incrementa `[0x8C1DD5B4]`) |
| `8C1C6D08` | 209 084 | 208 853 | **não** | linha de varredura do `SPG_STATUS` voltar a 0 (início do quadro) |
| `8C17262A` | 74 859 | 74 823 | **não** | vsync do `SPG_STATUS` apagar |

Com 0 voltas sem progresso, mas parte das mesmas esperas:
- `8C172624`: vsync acender;
- `8C1C6CB0`: linha sair de 0.

Nesta rota, a condição de cada uma já estava satisfeita na primeira leitura.

As quatro não declaradas rodam **uma vez, no boot**:
- `8C172624`, `8C17262A` e `8C172804` em V2;
- `8C1C6D08` e `8C172EF2` em V13.

Cobertura: **um** roteiro (NEW GAME até a primeira área), em 8 000 e 16 000 VBlanks. As contagens
acima são do de 16 000 e coincidem com as do de 8 000, exceto `8C064A26`.

## Método

### 1. Voltas exatas e voltas sem progresso (dinâmico)

Build de investigação `-DBS_MODS=ON -DBS_WRITE_WATCH=ON -DBS_BUILD_GAME=ON`, com
`BS_WW_VOLTAS=<arquivo>`. Nunca é o produto: no build normal a sonda não é compilada.

- **Voltas.** Cada `BS_POLL` (back-edge) conta uma volta para o PC de retomada, que é o início
  do laço. Os polls de retorno de chamada (`BS_POLL_RET`, do `BS_CALL`) contam numa coluna à
  parte, porque um retorno pode cair no mesmo PC de um início de laço. Para os seis laços acima,
  a coluna de retornos é 0.
- **Volta sem progresso.** É um back-edge com o mesmo PC de retomada que a volta anterior **do
  mesmo laço**, ainda que outros laços rodem no meio (p.ex. dentro de uma chamada). A sonda
  guarda os 16 laços ativos mais recentes, cada um com o próprio estado, a própria janela de
  pilha e a própria marca de escrita. A volta conta quando:
  - todo o estado arquitetural da CPU é igual: R0–R15, banco R0–R7, SR com T, GBR, VBR, SSR,
    SPC, SGR, DBR, PR, FR/XF, FPSCR, FPUL, MACH e MACL;
  - não houve **nenhuma escrita** relevante desde a volta anterior: RAM, MMIO, VRAM, AICA, store
    queue ou host. A exceção é a pilha abaixo do `r15` do laço (64 KB), onde ficam os frames das
    chamadas feitas dentro da volta.

  Um laço determinístico nesse estado repetiria para sempre. Só sai por mudança externa
  (hardware, interrupção, DMA, outra thread), e isso é a definição de espera. O critério vale
  também para laços que chamam funções, como o getter de `8C1C6D08`.
- **Exclusão da pilha.** É uma hipótese, não uma prova: um ponteiro antigo para um frame
  desempilhado, ou outra thread, poderia ter memória viva nessa faixa. Uma escrita escondida ali
  só pode gerar falso **positivo** (um laço de trabalho tomado por espera), nunca esconder uma
  espera. Por isso, toda detecção é conferida à mão no disassembly; as seis foram.
- **Correções do critério durante a investigação:**
  - a primeira versão comparava só os registradores inteiros e acusava `8C054A26`, um passo em
    ponto flutuante (`fsub fr3,fr13` a cada volta). Com a FPU no estado, ele sai;
  - a segunda guardava só o último back-edge global, e um laço externo com outro laço dentro da
    volta nunca era comparado consigo mesmo (falso negativo, revisão do Codex). Corrigido com
    os 16 laços ativos.
- **Identidade do laço.** É o PC de retomada (início). Passar o PC de origem de cada back-edge
  exigiria mudar o gerador. Então vários back-edges com o mesmo alvo dividem a contagem: na rota,
  são 804 inícios de laço e 833 back-edges estáticos associados.

### 2. Classificação estrutural de todos os laços executados

Feita por `tools/dev/n2b_lacos_quentes.py` e `tools/dev/n2b_classifica_lacos.py`:
- cruza as voltas com **todos** os desvios para trás do binário (`bt`, `bf`, `bt/s`, `bf/s`,
  `bra`), sem corte de tamanho nem de frequência;
- o corpo inclui o delay slot só quando o back-edge tem um.

Os laços sem escrita nem chamada no corpo são classificados pela leitura de memória:
- **sem leitura:** só registradores;
- **ponteiro móvel:** toda base escrita no laço; um literal recarregado não conta;
- **endereço fixo:** revisado à mão.

| Classe (16 000 VBlanks) | Laços | Conclusão |
|---|---|---|
| Com escrita no corpo | 649 | Ficam com o critério dinâmico (1) |
| Sem escrita, com chamada | 116 | Ficam com o critério dinâmico (1); foi assim que apareceu `8C1C6D08` |
| Sem escrita nem chamada, sem leitura | 10 | Computação |
| Sem escrita nem chamada, ponteiro móvel | 41 | Travessia de dados |
| Sem escrita nem chamada, endereço fixo | 17 | 4 esperas (`8C064A26`, `8C172804`, `8C172EF2`, `8C17262A`) e 13 computações (abaixo) |

Os 13 laços de endereço fixo que não são espera terminam por um registrador que muda a cada
volta. A leitura fixa é só o limite ou a raiz da estrutura:

| Laço | Saída |
|---|---|
| `8C02DD24` | `r4` += 1 até o limite `@r0`; `r5` avança 2 |
| `8C17864C` | `r5` += 1 até `@(28,r13)` (contagem de handles ADX) |
| `8C1865E2` | `r6` += 1 até `r5`; `r4` avança 12 (comparação de floats) |
| `8C1C3744`, `8C1C36E0` | lista ligada: `r4 = @(8,r4)` até 0 |
| `8C18A8EE` | `r4` += 1 até `@r6` |
| `8C17A2AA` | `r14` += 1 até `r6` (comparação de 4 bytes) |
| `8C1C2A06` | `r4` += 1 até `@(16,r15)` |
| `8C089B6A` | `r12` += 1 até `r4` |
| `8C1C29B4` | `r5` += 1 até `r6` |
| `8C05DFC0` | `r14` += 1 a cada volta, até achar o byte |
| `8C1C29A4` | lista: `r4 = @(4,r4)` até 0 |
| `8C174FAC` | `r4` += 1 até `@r6` |

### 3. Varredor estático, complementar

`tools/dev/n2b_esperas_estaticas.py game-data/1ST_READ.BIN`:
- analisa todo desvio para trás de até 96 bytes;
- acompanha as constantes de registrador a partir de 24 instruções antes;
- marca o laço quando ele lê uma região temporal ou de estado assíncrono (SPG, `ISTNRM`/`ISTEXT`/
  `ISTERR`, DMA do SB, Maple, GD-ROM, G1/G2, DMA do PVR, AICA, TMU, RTC, DMAC, INTC);
- ou quando chama uma função que lê uma dessas regiões nas primeiras 48 instruções, ou uma
  syscall do BIOS.

Ele acha as esperas de 0 voltas, que o dinâmico não vê. **Não** acha a de `8C1C6D08`, porque o
getter é chamado por registrador carregado fora do laço.

| Laço (início → back-edge) | Lê | Voltas | Classe |
|---|---|---|---|
| `8C172624` → `8C172628` | `SPG_STATUS` | 0 | Espera não declarada (satisfeita na 1ª leitura) |
| `8C17262A` → `8C17262E` | `SPG_STATUS` | 74 859 | Espera não declarada |
| `8C172804` → `8C172808` | `ISTNRM` | 474 534 | Espera não declarada |
| `8C1C1DC2` → `8C1C1DC6` | `C2DST` | 0 | Polling satisfeito sincronamente (o DMA conclui na escrita) |
| `8C096EE0` → `8C096EE8`/`8C096EF8` | `SB_FFST` bit 0, via `8C09A054` | 0 | Polling satisfeito sincronamente (o runtime responde 0) |
| `8C09724C` → `8C097254`/`8C097264` | `SB_FFST` bit 5, via `8C09A1F4` | 0 | idem |
| `8C17306A` → `8C173086` | TMU, via `8C15D1A2` | 238 937 | Espera declarada |
| `8C0978DE` → `8C0978F6` | TMU, via `8C15D1A2` | 0 | Espera declarada, não exercitada |
| `8C15BC7E` → `8C15BC98` | TMU, via `8C15D1A2` | 0 | Substituída no provedor nativo (`gdFsStop`) |
| `8C08E1E6` → `8C08E23E` | TMU, via `8C15D1A2` | 20 933 | Laço com contador (`r12`), 0 voltas sem progresso |
| `8C1991F0` → `8C19921A` | syscall, via `8C09A230` | 0 | Laço com contador (`r9`) |
| `8C08F668` → `8C08F6AC` | — | — | Falso positivo (dado do pool de literais) |
| `8C090044` → `8C090078` | — | — | idem |

## Esperas não declaradas: detalhe

| Laço | Função (chamador) | Quando | Saída | Timeout |
|---|---|---|---|---|
| `8C172624`, `8C17262A` | `f_8C172620` (de `f_8C172642`, modo de vídeo) | V2 | vsync aceso, depois apagado | nenhum |
| `8C172804` | `f_8C172642` (configura `VO_CONTROL`) | V2 | `ISTNRM` bit 3 aceso | nenhum |
| `8C1C6CB0`, `8C1C6D08` | `f_8C1C6B3C` (de `8C1C6F9E`; getter `8C1C59D6` = `[0xA05F810C]`, máscara `0x3FF`) | V13, uma chamada | linha sai de 0, depois volta a 0 | nenhum |
| `8C172EF2` | `f_8C172D22` (de `8C0788A0`) | V13 | `[0x8C1DD5B4]` mudou | nenhum |

No N2b-2, cada uma tem de ser declarada com o evento que a libera, como um instante do relógio
lógico calculado pela configuração do SPG. Não é avanço artificial:
- VBlank-In para `8C172804` e `8C172EF2`;
- bordas do vsync para `8C172624` e `8C17262A`;
- linha 0 para `8C1C6CB0` e `8C1C6D08`.

Até lá, a guarda de liveness pararia nelas com o PC.

## Proveniência

Os arquivos brutos ficam em `analysis/n2b/` (ignorado pelo git), com `voltas_proveniencia.txt`:
- o hash do executável e do `1ST_READ.BIN`;
- o commit, o hash de `fontes.patch` (`git diff HEAD` no momento da medida) e o hash de cada
  arquivo novo;
- o comando de cada execução;
- o hash dos TSV.

Rodada final (2026-10-16, 20:56):
- executável `26D12CA7…`;
- `voltas_area1.tsv` `A4522E86…`;
- `voltas_exploracao.tsv` `53916AA2…`.

Os mesmos TSV saíram, byte a byte, com a versão anterior do critério (só o último back-edge e
sem `VBR`/`SSR`/`SPC`/`SGR`/`DBR`). As duas correções não acharam nenhuma espera nova.

Para refazer, use os dois comandos registrados lá e depois:

```
python -I tools/dev/n2b_lacos_quentes.py game-data/1ST_READ.BIN analysis/n2b/voltas_exploracao.tsv > analysis/n2b/voltas_exploracao_lacos.tsv
python -I tools/dev/n2b_classifica_lacos.py game-data/1ST_READ.BIN analysis/n2b/voltas_exploracao_lacos.tsv
```

## O que este inventário não prova

- **Outras rotas:** troca de sala, combate, save/load, menus não visitados, finais (U7).
- **Esperas de 0 voltas fora do varredor estático:** por ponteiro calculado ou chamada indireta,
  como `8C1C6CB0` e `8C172624` seriam se o estático não as visse.
- **Esperas que mudam o estado a cada volta:**
  - um contador de timeout em registrador;
  - uma função chamada que escreve fora dos próprios frames.

  Elas não têm volta sem progresso. Entre os 68 laços sem escrita nem chamada, nenhuma é desse
  tipo (revisão acima). Nos 765 com escrita ou chamada, essa forma não está auditada. No N2b-2,
  uma espera com contador não trava: sai pelo timeout, mas por um caminho diferente do legado.
- **Laços fechados por desvio indireto** (`jmp`, `braf`) ou por chamada repetida não passam por
  `BS_POLL` como back-edge.
