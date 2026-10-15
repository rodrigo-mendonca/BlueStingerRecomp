# N1: sistema de arquivos nativo (relatório)

Data: 2026-10-15. Plano e critérios: `docs/N1-PLANO.md`. Base: N0 (`e267149`). Aprovado pelo
mantenedor em 2026-10-15, com um commit único autorizado. N2 não iniciado.

## 1. Resumo

| Pergunta | Resposta (com evidência) |
|---|---|
| O jogo lê os recursos por uma interface própria? | **Sim.** A API gdFs (17 funções, mais o gancho no servidor gdFs que conclui os pedidos assíncronos) foi substituída por um adaptador sobre `native::files::IFileSystem`, implementado por `DataFileSystem`: arquivos do Windows em `data\disc` e `mods\assets`. É o provedor **padrão**. |
| Ainda há CDI, setores ou comandos GD-ROM no caminho migrado? | **Não.** No roteiro NEW GAME (8 000 VBlanks) foram 68 aberturas e 108 leituras (15,8 MB) pelo provedor nativo, com **0** setores de dados de arquivo pelo GD-ROM HLE (P2). A implementação nativa não usa LBA, FAD, ISO 9660 nem `disc.manifest`. |
| Os recursos carregados são os mesmos? | **Sim.** Mesma sequência global de 7 750 blocos de 2 048 bytes `(arquivo, deslocamento, SHA-256)`, nos mesmos arquivos e na mesma ordem, legado × nativo (P1). Arquivos com o mesmo conteúdo no disco (88 grupos que dividem setores, p.ex. `IWP15.NT`/`IWP20.NT`) são comparados pelo grupo (seção 5). |
| O comportamento do jogo é o mesmo? | **Mesmos recursos, na mesma ordem; o tempo de carga muda.** As cargas terminam em outro ponto do tempo emulado, e os eventos ficam deslocados de forma uniforme (+2 VBlanks depois da tela-título). Por isso o quadro num VBlank fixo difere. O nativo é determinístico (log e quadro iguais em duas execuções). Com `--arquivos legado`, o quadro é **idêntico** ao do build do N0. |
| Mods de recursos | Valem pelo provedor nativo, **de qualquer tamanho**, verificados: o mod tem de substituir um recurso existente e ser arquivo legível, senão há erro com o caminho e o motivo. Os aplicados são registrados (`[mods] recurso substituido: … (a -> b bytes)`). |
| Eliminação do hardware original | **Não concluída.** Restam caminhos legados (seção 6). |

## 2. Identificação da API (por contrato)

Evidências:
- disassembly do módulo gdFs (`0x8C15A5E8`-`0x8C15C300`);
- grafo de chamadas;
- trace de argumentos e retornos (`BS_CALL_TRACE`, novo diagnóstico, build BS_MODS).

Contratos e estrutura GDFS estão em `docs/N1-PLANO.md`, seção 2:
- 21 endereços que criam ou recebem GDFS, dos quais 17 têm código gerado e são substituídos;
- mais o servidor do gdFs (`8C15AE28`): o gancho conclui o pedido assíncrono nativo pendente e
  segue com o servidor legado;
- 4 nunca alcançados pela análise (GetFileFad, SetTransMode e dois registros de callback).

Chamadores observados:

| Chamador | Arquivos |
|---|---|
| Carregadores do jogo (`8C02A200`, `8C01C180`, `8C01C0E0`) | `MANATEE.DRV`, `BS_SND.MLT`, `.NT`, `.BIN`, `.PRM` |
| Carregador de modelos (`8C15E9FC`) | `.NJ` |
| Streaming do CRI (`8C1D92xx`, assíncrono) | `NEWGAME.SFD`, `E001.SFD`, `E012AENV.WAV` |

## 3. Implementação

| Peça | Onde | Dreamcast? |
|---|---|---|
| Interface | `native/platform/files.h` (`IFileSystem`, `canonical_path`) | não |
| Implementação | `native/platform/data_files.*` (`DataFileSystem`: índice pela árvore de pastas, mods verificados, leitura síncrona/assíncrona) | não (`n0_native_limpo`) |
| Adaptador | `transitional/gdfs_adapter.*` (contratos gdFs; handle `0x7E______` com geração; a unidade de 2 048 bytes é da API; o último setor é completado com zeros; `ReqRd32` publica "ocupado" (2) com 0 bytes e conclui no servidor gdFs seguinte, como no binário; um pedido por vez; `Stop`/`Close` cancelam) | só a convenção do guest |
| Provedor | `BoundaryRegistry` + slots sempre ativos (`[native] slots`; gerador `BSG_SLOT`) | — |
| Seleção | `--arquivos nativo` (padrão) \| `legado` (referência e rollback) | — |
| Guardas | handle nativo em função sem equivalente → erro fatal com mensagem; GDFS legado → função original; instalar sem slot → erro | — |
| Diagnóstico | `BS_FILE_LOG` (contrato P1, com VBlank), `BS_CALL_TRACE`, `RuntimeDisc::locate` | legado |

O gerador com slots sempre ativos é **neutro**: o quadro em 6 000 VBlanks é igual ao do N0
(`b49d68e2…`).

## 4. Portões

| # | Critério | Resultado |
|---|---|---|
| P1 | Mesmas leituras | **ok**: sequência global idêntica (7 750 blocos, 54 arquivos, mesma ordem), comparando arquivos de mesmo conteúdo pelo grupo |
| P2 | 0 setores de arquivo pelo GD-ROM no modo nativo | **ok** (0; restam 2 setores "outro" de inicialização, contra 49 no legado) |
| P3 | Comportamento | **ok com diferença de tempo explicada** (seção 5): mesmos recursos e ordem; eventos +2 VBlanks; roteiro termina; `test_gameplay` passa |
| P4 | Interface independente | **ok** (`n0_native_limpo` cobre `data_files.cpp`; testes `n1_*`) |
| P5 | Mods | **ok** (unitários; `test_projeto`, ver seção 7) |
| P6 | Distribuição | ver seção 7 |
| P7 | Fronteira | **ok**: 5 entradas gdFs da fronteira com provedor nativo; `fronteira.provedor.legado` 267 → 262; `legado.subsistema.arquivos` 17 → 2 |
| P8 | Rollback | **ok**: `--arquivos legado` dá o quadro do N0 em 6 000 VBlanks (`b49d68e2…`) |

## 5. Investigação da diferença (legado × nativo)

**O que se viu primeiro:** o log do legado registrava `IWP20.NT` e o do nativo `IWP15.NT` na
carga da 1ª sala, e o quadro em 8 000 VBlanks diferia (`F472ECC2…` × `F7818C7E…`, cada um
repetível).

**Hipóteses descartadas, com experimento:**

| Hipótese | Experimento | Resultado |
|---|---|---|
| Tempo de carga (leituras nativas não consomem ciclos SH-4) muda uma escolha do jogo | Diagnóstico temporário cobrando de 0 a 100 000 ciclos por setor no nativo | O VBlank da carga foi de 2262 para 2286 e o arquivo continuou `IWP15`. Descartada para a escolha. O diagnóstico foi removido. |
| Bytes depois do fim do arquivo no último setor (o legado entrega sobras, o nativo completa com zeros) | Conferência das sobras em `disc_meta` | Nenhum dos 2 092 arquivos tem sobra não nula. Descartada. |
| Valor do handle (`0x7E…` positivo × `0x8C…` negativo) | Etiqueta trocada para `0xFE000000` | Continuou `IWP15`. Descartada. |
| Protocolo assíncrono (revisão do Codex: estado "ocupado" e conclusão no servidor) | Protocolo corrigido conforme o binário | Mesmo quadro e mesmo arquivo. A correção ficou: é o contrato. |

**Causa real (provada):** `IWP15.NT` e `IWP20.NT` são **o mesmo arquivo no disco**: mesmo LBA
(268743), mesmo tamanho, mesmo SHA-256.
- O disco tem **88 grupos** de nomes que dividem os mesmos setores.
- O log do legado identifica o arquivo pelo setor (`RuntimeDisc::locate`), então não distingue
  os nomes e escolheu `IWP20`.
- O jogo abriu `IWP15` **nos dois modos**. Evidências:
  - o trace de chamadas mostra `r4="IWP15.nt"` na chamada ao carregador `8C01C180` (via
    `8C05E420`, que monta o nome a partir da tabela de registros em `0x8C150DE0` + índice ×124
    + 100);
  - os snapshots de RAM (`BS_RAM_DUMP`, novo diagnóstico) em 2250 e um VBlank antes de cada
    abertura mostram o mesmo índice (`0x8C24FBE1` = 08 04 01) e o mesmo nome nos dois modos.
- Com a comparação por grupo de conteúdo, a **sequência global de leituras é idêntica**.

**A diferença restante é só de tempo:**
- as cargas do boot terminam 1 VBlank antes no nativo;
- da tela-título em diante, os eventos ficam +2 VBlanks no nativo, uniforme até o fim do roteiro;
- o quadro num VBlank fixo, portanto, mostra outro instante do jogo.

O legado conta como tempo o custo de executar o código gdFs e os laços de espera emulados. O
nativo não emula esse custo, por decisão: nada de simular o hardware no caminho nativo. O N2
(relógio lógico nativo) redefine a base de tempo.

**Consequência para os portões:** igualdade de quadro com o legado deixa de ser critério
quando um subsistema migra. A referência passa a ser:
- o contrato de leituras (P1);
- o roteiro de jogo (`test_gameplay`);
- o determinismo do próprio provedor nativo (trace de ordem e quadro repetíveis).

## 6. Cobertura e caminhos restantes (legados)

O caminho de **leitura de arquivos** do jogo, do CRI e do Ninja é nativo. Continuam legados:

| Caminho | Por quê | Quando sai |
|---|---|---|
| Inicialização do gdFs (`8C08BB40` → `gdFsInit`): TOC, descritor e diretório raiz pelo GD-ROM HLE (2 setores) | Não recebe GDFS; o servidor do gdFs depende do estado montado | Etapa seguinte do N1 ou N8 |
| Estado do drive (`8C15C016`, jogo ×16; `8C15BFD2`, CRI ×2 M) e servidor por quadro (`8C15A644`) | Consultam o drive (bandeja, disco) | Idem |
| Boot (IP.BIN e executável por `RuntimeDisc`) | Parte do BIOS HLE | N8 |
| GD-ROM HLE, `disc_meta`, `RuntimeDisc::readSector` | Consumidores acima | Quando não houver consumidor |
| 4 funções gdFs sem código gerado | Inalcançáveis | — |

Nada disso conta como eliminação do hardware original.

## 7. Verificação

Em 2026-10-15, no código final (depois das correções da revisão do Codex):

| Verificação | Resultado |
|---|---|
| Unitários (`ctest` sem dados) | 192/192 (10 `n1_*`) |
| `ctest` com o CDI real | 11/11 |
| Catraca de métricas (`metricas_base_us.toml`) | ok; fronteira regenerada |
| `make_builder.ps1`, `make_preview.ps1` (Preview gerada pelo próprio Builder) | ok |
| `test_preview` | 9/9 |
| `test_gameplay` (NEW GAME até a 1ª área, provedor nativo padrão) | 4/4 |
| `test_builder` (CDI → BlueStinger.exe) | 18/18 |
| `test_projeto` (projeto editável; mod maior aceito no nativo; erro claro para mod sem recurso e, no legado, para mod maior) | 11/11 |
| `test_arquivos` (P1, P2, P3, P8; 8 000 VBlanks) | 6/6: 7 750/7 750 blocos, 54 arquivos iguais, ordem global igual, 0 setores de arquivo pelo GD-ROM no nativo, 68 aberturas e 108 leituras nativas (15 797 116 bytes), 0 não encontrados. Quadro final legado `F472ECC2…` × nativo `F7818C7E…` (seção 5) |
| `test_ordem` | 8/8 (ordem `176F4C28…` igual em duas execuções) |

Verificação pré-commit (2026-10-15, depois da aprovação): builds debug e release sem nada a
recompilar (os binários são os das suítes acima); `ctest` com o CDI real 11/11 (`boot_trace`
pulado: precisa do oráculo do Flycast). Conferido que nada de `ISO/`, `game-data/`, `generated/`,
`analysis/`, `reviews/`, `dist/` ou `snapshots/` entra no commit (todos ignorados; os arquivos
novos são fonte e documentação em texto, sem caminhos pessoais).

Revisão independente do Codex: `reviews/codex-20261015-101006.md`. Achados aceitos e rejeitados
em `STATUS.md` (N1).
