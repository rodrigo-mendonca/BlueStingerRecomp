# N3-3: recursos de controles do produto

Data: 2026-10-18/19. Autorização do mantenedor: **N3-3**, sobre `ba4fc2f` (N3-2). Plano:
`docs/N3-PLANO.md`. O padrão continua `--controles legado`. Vibração e mouse ficaram fora do escopo:
o N3-0 não achou uso de `pdVib`, e o mouse foi excluído na aprovação do N3-0.

## 1. Escopo

| Requisito (plano) | Situação |
|---|---|
| Remapeamento completo (arquivo e documentação) | motor do N3-1; documentação nova (`docs/CONTROLES.md`, `LEIA-ME.txt`, comentários do arquivo padrão) |
| Perfis por GUID de dispositivo | **novo**: `[perfis."<guid>"]` |
| Vários controles, troca a quente | N3-1 (troca pelo último com atividade, preferido por nome; conectar não toma o jogador) |
| Desconexão/reconexão com o mesmo resultado do legado | N3-2 P3 (porta some e volta em passos fixos, nos dois modos de tempo) |
| Teclado com mapa padrão revisado | revisado e **mantido**: cobre todas as ações que o jogo usa e é compatível com `--map` |
| O remapeamento produz no jogo o efeito dos botões equivalentes | **novo**: `test_pd_nativo` P9 |
| Xbox e teclado testados manualmente na Preview | teclado testado (eventos reais na janela); **controles físicos pendentes** (não há hardware na máquina) |

## 2. Perfis por GUID

- Chave: o GUID do SDL3 (32 dígitos hexadecimais, sem diferença de maiúsculas), ou `xinput` para os
  controles do XInput de reserva. O log mostra o GUID de cada controle ao conectar, com o perfil
  aplicado ou "mapa [controle]".
- O perfil herda o `[controle]` e substitui só as ações que lista; `nome` e `zona_morta` são
  opcionais.
- **Fallback seguro:** controle sem perfil (GUID desconhecido ou vazio) usa o `[controle]`. Um perfil
  inválido (GUID malformado ou repetido, ação ou controle desconhecido, zona morta fora da faixa)
  invalida o arquivo inteiro com linha e motivo, e o jogo avisa e usa o mapa padrão. É o mesmo
  tratamento do N3-1.
- Código: `native/platform/input.h` (`Profile`, `ActionMap::profile_for`, `ConnectionEvent::guid` e
  `profile`), `input_map.cpp` (parse depois do `[controle]`), `input_system.cpp` (`padState` pelo
  perfil do controle; XInput com GUID `xinput`), `app/main_sdl.cpp` (`SDL_GetGamepadGUIDForID`).

## 3. Testes

- **Unitário** `input_native_profiles_by_guid`: parse, herança, maiúsculas, zona morta do perfil,
  `xinput`, fallback, evento com GUID e perfil, cinco erros, arquivo padrão sem perfis. 236 unitários.
- **P9** (`test_pd_nativo`, modo nativo, `passo_limiares`, 9 558 passos). O diagnóstico
  `BS_CONTROLE_FISICO` lê um roteiro como controles físicos na posição padrão e os aplica a um controle
  simulado num `InputSystem` real, com `BS_CONTROLES_TOML` e `BS_CONTROLE_GUID`. Resultados:

  | Execução | Resultado |
  |---|---|
  | roteiro como controles físicos, mapa padrão | traço com tempo e quadros = roteiro lógico |
  | A↔B trocados no roteiro físico e no `[controle]` | = roteiro lógico |
  | A↔B trocados no roteiro físico e só num perfil, com o controle desse GUID | = roteiro lógico |
  | negativo: o perfil é de outro GUID (fallback para o `[controle]`) | diverge (2 243 passos) |

- **Janela** (controle virtual do SDL3 e teclado real):
  - o GUID do SDL3 aparece no log;
  - um `[perfis."<GUID do controle virtual>"]` com A↔B faz o `a` chegar ao jogo como B ("perfil
    virtual" no log), e o teclado continua no mapa dele;
  - desconexão e reconexão no log.
- **Perfis com hotplug** (`input_native_profiles_with_hotplug_and_xinput`):
  - dois controles com perfis diferentes na troca automática;
  - desconectar o ativo volta ao outro, com o perfil dele;
  - o evento de desconexão leva o perfil;
  - reconectar o mesmo GUID reaplica o perfil;
  - `controle_preferido` prevalece sobre a atividade;
  - `[perfis.xinput]` aplicado pelo leitor XInput injetado.
  237 unitários.
- **P9, conexão com perfil:** o roteiro físico de `passo_conexao` (A↔B trocados, perfil pelo GUID)
  desconecta o controle com botão apertado e o reconecta com o mesmo GUID. No produto o teclado
  continua presente, então a porta do jogo fica neutra, não vazia. O resultado é igual ao roteiro
  lógico com a desconexão como estado neutro (9 498 passos). A desconexão com a porta vazia (só por
  roteiro lógico) já tem a equivalência com o legado no P3 do N3-2.

## 4. Revisão do Codex (`reviews/codex-20261018-224640.md`)

| Achado | Decisão |
|---|---|
| [MÉDIA] o negativo do P9 passaria se a execução falhasse (qualquer código ≠ 0 do comparador) | aceito: `RunAll` guarda o processo de cada execução; `ExitOk` exige código 0 e traço não vazio; os negativos (P9 e, pelo mesmo motivo, os do P6) exigem "diferentes > 0" |
| [MÉDIA] desconexão/reconexão com perfis e troca entre controles não exercitadas | aceito: `BS_CONTROLE_FISICO` aceita `DESCONECTADO` (tira o controle e o reconecta com o mesmo GUID); P9 com `passo_conexao`; unitário de hotplug com dois perfis, preferido e `xinput` |
| [BAIXA] o evento de desconexão perdia o perfil | aceito: o `Pad` guarda o perfil resolvido |

Da correção do teste saiu uma semântica para documentar: ligar uma ação a um botão não tira o botão
da ação padrão dele. Para trocar, listam-se as duas (`docs/CONTROLES.md`, "Trocas").

## 5. Promoção do nativo a padrão (decisão do mantenedor, 2026-10-19)

Aplicada **depois** da regressão completa do N3, que validou o código com o padrão anterior
(`legado`).

**Alterações:**

| Arquivo | Mudança |
|---|---|
| `app/main_sdl.cpp` | `Args::controls = Native`; o `--help` diz "padrão: nativo; legado = fallback temporário pelo Maple"; o comentário "em validação" sai |
| `app/game_host.h` | `GameOptions::controls = Native` (o mesmo padrão de quem monta o host sem o `main_sdl`) |
| `tools/dist/test_controles.ps1` | `--controles legado` em todos os casos K1–K12: é a suíte de referência do Maple (sondas `BS_PAD_CONTRATO`, que o nativo recusa por desenho, `BS_MAPLE_FUNCAO`, `BS_PAD_MUTACAO`). O K13 fica **sem** a opção: compara o padrão novo com o executável anterior ao N3 (quadros e leituras), o que prova a promoção |
| `tools/dist/test_n2b2.ps1` | N11 (byte a byte, com ciclos, contra o executável anterior ao N2b-2) passa a usar uma execução própria `est_n11` com `--controles legado`. No nativo o DMA do Maple termina ~9 600 ciclos antes (N3-2, seção 8), então o trace com ciclos difere por desenho. Os outros critérios continuam no padrão |
| `tools/dist/test_pd_nativo.ps1` | caso novo **P10**: sem `--controles`, o fechamento indica modo nativo e os artefatos são iguais aos do `--controles nativo` explícito |
| `docs/CONTROLES.md`, `LEIA-ME.txt`, `CLAUDE.md` | padrão nativo; `--controles legado` como fallback temporário |

**Sem mudança:**
- `test_pd_nativo` P1–P9 já passam `--controles` explícito;
- o Builder e a exportação C++ (o executável do Builder sai do mesmo `main_sdl`; o C++ gerado não
  depende do provedor);
- o mapa, os perfis e o `controles.toml`.

**Testes afetados.** A troca vale para toda execução sem `--controles`. O que roda de novo:
- builds, unitários, `ctest` com e sem disco;
- `make_preview`, `test_preview`, `test_gameplay`, `test_builder`, `test_projeto`, `test_assistido`
  (fluxos do produto, agora no nativo);
- `test_tempo` e `test_arquivos` (a combinação nova `--tempo legado` + controles nativos; o `test_tempo`
  tem quadros de referência gravados);
- `test_controles` (fixado no legado; o K13 prova o padrão novo);
- `test_n2b2` (N11 alterado);
- `test_pd_nativo` (P10 novo).

**Não roda de novo**, com o motivo:
- `test_ordem`, `test_cri`, `test_efeito` e `test_n2b`: comparam execuções do mesmo executável entre
  si, todas com o mesmo provedor de controles;
- o P1 do N3-2 provou nativo = legado em quadros, leituras, cargas, estado e áudio nos dois modos de
  tempo, e o `test_tempo` cobre o `--tempo legado` com os controles novos.

A regressão completa fica para o próximo fechamento de etapa principal (política de 2026-10-18).

**Resultados (2026-10-19):**
- Regressão completa do N3 com o padrão anterior: 19 de 19 etapas aprovadas, 0 falhas (`regressao.ps1`, fonte `803FB171F8544023`, de 18/10 23:06 a 19/10 03:32). Destaques: `test_controles` 39 casos, `test_pd_nativo` 83.
- Depois da promoção:
  - builds release, debug, mods e watch;
  - unitários: 245 de 245, os 237 do N3-3 e os 8 do domínio de saves do N4;
  - `ctest` com e sem disco: 100%.
- Suítes afetadas, todas com 0 falhas:

  | Suíte | Casos |
  |---|---|
  | `make_builder`, `make_preview` | ok |
  | `test_preview` | 9 |
  | `test_gameplay` | 4 |
  | `test_builder` | 18 |
  | `test_projeto` | 13 |
  | `test_arquivos` | 6 |
  | `test_tempo` | 35 |
  | `test_n2b2` | 17 |
  | `test_assistido` | 28 |
  | `test_controles` | 39 |
  | `test_pd_nativo` | 85 |

  - No `test_controles`, o **K13 passa com o padrão nativo**: quadros e leituras iguais aos do executável anterior ao N3, nas três rotas.
  - No `test_pd_nativo`, o **P10** confirma que, sem a opção, o fechamento indica `modo=nativo`, com o servidor e o decodificador do SDK e o Maple em 0, e que o traço com tempo, os quadros, as leituras, o estado e o áudio são iguais aos do `--controles nativo`.
  - O Builder, o projeto exportado, os mods e o produto sem CDI estão cobertos por `test_builder` e `test_projeto`.

**Validação do produto** (Preview de 19/10 03:36, `ba4fc2f-dirty`):
- **Janela sem parâmetros,** com o controle virtual do SDL3 (`BS_CONTROLE_VIRTUAL`, com `conectar` e depois START e A):
  - log `[controles] conectado: Controle virtual (GUID ff00a533…7601; mapa [controle])`;
  - no traço do PDS, START (bit 3) e A (bit 2) chegam ao jogo;
  - fechamento `modo nativo`: servidor `8C15C4E6` com 0 entradas, 0 respostas do controle no Maple e 340 passadas do servidor nativo.
- **`--controles legado` na mesma Preview:** os mesmos botões chegam pelo Maple (servidor original 340, respostas do Maple 690, servidor nativo 0).
- **Sem parâmetros e sem roteiro, por 25 s:** o jogo roda sem erro no modo nativo.
- **Lançador:** é o próprio `BlueStinger.exe`, com dois cliques e sem parâmetros. O `LEIA-ME.txt` indica o fallback.
- **Controle Xbox físico:** pendente do mantenedor, pelo roteiro da seção 6. Não é declarado aprovado.

## 6. Pendências e riscos

### Roteiro do teste manual (controle Xbox físico, Preview)

1. Conectar o controle (USB ou Bluetooth) **antes** de abrir. Abrir `BlueStinger.exe` com dois
   cliques, sem parâmetros.
2. `bluestinger.log` (ao lado do executável): conferir as linhas `[controles] mapa: ...` e
   `[controles] conectado: <nome> (GUID <32 dígitos>; mapa [controle])`. Anotar o GUID.
3. Título: **Start** abre o menu. **D-pad** e **analógico esquerdo** navegam. **A** confirma e **B**
   volta.
4. NEW GAME até o controle do personagem:
   - o analógico anda, com resposta proporcional, e parado o personagem fica imóvel (sem *drift*);
   - os gatilhos LT/RT respondem a partir da metade do curso;
   - X e Y fazem as ações do jogo.
5. Desconectar o controle no meio do gameplay. O log registra `desconectado`, nada fica "preso" e o
   teclado continua. Reconectar: volta a responder, com o mesmo GUID no log.
6. Opcional:
   - um `[perfis."<GUID>"]` com `A = "b"` e `B = "a"` no `controles.toml` troca os dois botões só
     nesse controle;
   - apagar o arquivo volta ao padrão.
7. Fallback: `BlueStinger.exe --controles legado` repete os passos 3–4 com o mesmo resultado.

Anotar para o relatório: modelo do controle, conexão (USB, Bluetooth), GUID, passos que falharam.

- **Controles físicos** Xbox e PlayStation: não testados (não há hardware nesta máquina). Os GUIDs
  reais e a estabilidade deles entre conexões e portas USB não foram verificados. Critério de aceitação
  manual do N3 **pendente**.
- O P9 cobre A↔B, analógico, gatilhos e direcional do roteiro de limiares. Não cobre o remapeamento de
  eixos invertidos nem a zona morta por perfil no caminho completo do jogo; estes têm só unitários.
- A desconexão física no produto não esvazia a porta, porque o teclado fica. É a decisão de produto do
  N3-1. Pausa automática ao desconectar é melhoria futura, fora do modo fiel.
