# Blue Stinger Native PC: auditoria e plano de migração (v2)

Status: **PROPOSTA, aguarda aprovação do mantenedor**. Nenhuma mudança estrutural foi feita
por causa deste plano.
- v1 (2026-10-15) revisada pelo Codex: `reviews/codex-20261015-003938.md`, 6 achados altos
  e 5 médios, todos incorporados aqui (seção 9).
- v2: 2026-10-15.

Diretriz (mantenedor, 2026-10-15): o produto final é um port nativo para Windows x64.
- Não é um Dreamcast simulado: sem CPU virtual (SH-4/ARM7), sem PowerVR2, AICA, BIOS,
  GD-ROM, Maple/VMU simulados, sem registradores/endereços do Dreamcast como mecanismo de
  execução, sem emulador e sem CDI em tempo de execução.
- As ferramentas e o Flycast continuam permitidos em desenvolvimento, conversão e
  validação.
- A migração é incremental: nada que funciona hoje é descartado de uma vez.

Substitui o "Plano de áudio nativo" de ARCHITECTURE.md (ARM7 interpretado, agora
proibido).

---

## 1. Auditoria (estado em 2026-10-15)

### 1.1 Métricas
Todas devem virar um relatório gerado e versionado em N0, com comando e definição de cada
uma.

| Métrica | Valor | Definição |
|---|---|---|
| Funções do jogo | 4 083 | `analysis/report.txt`: 1 329 por chamada, 2 605 por ponteiro em dado, 77 por tabela de ponteiros, 64 por pool, 7 do config |
| Código gerado | ~977-981 mil linhas | Arquivos `generated/funcs_*.cpp` |
| Polls de eventos | 2 287 | `BS_POLL` emitidos (back-edges) |
| Despacho indireto | 2 576 sites analisados / 2 601 invocações emitidas de `sh4_dispatch` | |
| Chamadas diretas | ~15 mil | `BS_CALL` |
| Operações de FPU | 71 999 | Chamadas `bs::op::F*` (a v1 dizia ~115 mil, contando linhas com comentários) |

**Acessos a hardware no código gerado** (varredura do Codex: literais de pool resolvidos no
`1ST_READ.BIN`; N0 tem de reproduzir com ferramenta):

| Faixa | Cargas | Endereços / funções |
|---|---|---|
| Holly/PVR/Maple/G2 | 70 | 37 endereços, 34 funções |
| AICA | 6 | 4 funções |
| VRAM | 8 | 7 funções |
| Faixa de store queue (P4/SQ → TA) | **439** | **288 funções** (ex.: `8C16C7A0` com `pref` em `8C16C828`) |
| DMAC | 15 | 5 funções (ex.: `8C1C1D4C`: FFA000xx e A05F6884) |

### 1.2 Bibliotecas do SDK Katana embutidas
Faixa de strings 0x8C1DAAF0..0x8C1DC8F0. Todas as strings observadas vão para o banco de
assinaturas, sem normalizar.

| Biblioteca | Versão observada |
|---|---|
| Shinobi | 1.28 (`syStart` 1.02, gdFs 1.05, pad/per, backup) |
| Ninja | Ver 0x00060068, Nov 04 1998 |
| Kamui | KAMUI2 |
| CRI SFD (Sofdec) | 1.04 |
| CRI ADXF | **"Version 5.30" e "Ver.5.42 Build"** (as duas strings existem) |
| CRI MWGSC | 1.11 |
| CRI MWRNA | 1.07 |
| Biblioteca de som do lado SH-4 | Carrega `MANATEE.DRV`/`BS_SND.MLT` |

Os nomes de API (`njDrawModel`, `gdFsRead`, `kmRender`...) **não** aparecem no binário. As
strings só localizam as bibliotecas; identificar as entradas exige outras evidências
(seção 3).

### 1.3 Classificação por subcomponente

| Classe | Subcomponentes |
|---|---|
| **Ferramentas** (conversão/análise/validação, fora do produto) | Extrator, analisador, gerador, Builder, `bs-diff`, oráculo, testes |
| **Reutilizável no nativo** | Janela e eventos SDL3 (`app/main_sdl` sem a parte de jogo); relógio do host; fonte de entrada do host (teclado/XInput) sem a serialização Maple; manifesto de dados e leitura de arquivos (`RuntimeDisc::readFile`, `load_runtime_manifest`, `check_runtime_files`); empacotamento do Builder |
| **Transitório** (some ao fim) | `GameHost` atual (RAM guest de 16 MB, `Sh4Context`, boot guest); código gerado e `sh4_ops`; slots e ABI de mods `void(Sh4Context*)`; serialização de pad Dreamcast; `sh4_threads` (fibras por PC/R15) |
| **Legado de validação** (fora do produto) | Reconstrução de setores (`RuntimeDisc::readSector`), GD-ROM HLE, boot/BIOS HLE, `pvr/*` (TA, VRAM, registradores, rasterizador por software), Maple, AICA, periféricos do SH-4 (INTC, TMU, DMAC, SQ, CCN, portas), leitura de CDI (`boot_read_disc`, `bs_disc`) |

---

## 2. Arquitetura-alvo

```
tools/          conversão, análise, identificação, manifesto de hardware, assets
native/         sistemas definitivos (o que vai no BlueStinger.exe)
  platform/     janela, entrada (teclado, mouse, controles), tempo/quadros, arquivos, saves
  render/       renderizador de GPU (atrás de interface; backend a decidir)
  audio/        motor de som (bancos e sequências do BS_SND.MLT), saída nativa
  video/        decodificação de SFD (MPEG-1 + ADX), saída sincronizada
  sdk/          implementações nativas das APIs usadas pelo jogo (interfaces tipadas)
  game/         sistemas do jogo elevados para C++ com nomes e estruturas próprias
generated/      (privado) funções ainda não elevadas, que encolhe com o tempo
legacy/         simulação do Dreamcast: só em builds de validação
```

### Fronteiras em três camadas (substitui "slots = API nativa" da v1)
1. **Interface nativa tipada** (sem endereços guest, sem `Sh4Context`). Por exemplo,
   `files::open(path) -> Handle`, `render::submit(CommandList)`.
2. **Adaptador temporário** `Sh4Context ↔ interface nativa`: lê os argumentos de r4-r7 e da
   RAM guest, faz o marshaling, chama a interface e escreve r0/RAM. Some quando os
   chamadores forem elevados.
3. **Provedor selecionável em execução**: legado (o código gerado/simulação atual) ou
   nativo. O build híbrido **sempre** contém os dois e **slots sempre ativos nas fronteiras
   migráveis**, independente da opção de mods. O custo é medido em N0.

### Modelo de interoperabilidade da fase híbrida (antes de N1)
- **Handles de 32 bits** com geração, numa tabela no host: ponteiros do host nunca vão
  para a RAM guest.
- **Propriedade e tempo de vida**: quem cria e quem libera; o fim de sessão limpa tudo.
- **Marshaling explícito** das estruturas que cruzam a fronteira, com layout documentado e
  versão.
- **Callbacks guest ↔ nativo**: o nativo chama o guest pelo despacho; a ordem e a
  reentrância seguem o protocolo da seção 5.
- **Mapa de regiões da RAM guest** por sistema (quem é dono de quê).
- **Migração versionada** de estado e de saves.

---

## 3. Identificação e prova de fechamento (pré-requisito de toda etapa)

- **Identificação de função do SDK**: pelo menos **duas evidências independentes** entre:
  - referência a string ou tabela;
  - assinatura de bytes mascarada;
  - posição no grafo de chamadas;
  - convenção de argumentos;
  - teste de contrato (entrada → saída/efeito igual ao legado).

  Aliases, wrappers, callbacks, tail calls e entradas intermediárias ficam registrados. Uma
  família de API não é aprovada só pela proximidade do banner.
- **Manifesto de acessos a hardware**: cada acesso a MMIO/P4/SQ/VRAM/AICA/DMAC com a
  função, os chamadores e **a fronteira nativa que o domina**.
- **Prova de fechamento**: um módulo legado só sai quando nenhum caminho alcançável
  contorna suas fronteiras.
  - Estática: dominância no grafo, incluindo ponteiros e callbacks.
  - Dinâmica: o legado instrumentado não registra acesso nos roteiros de cobertura.

---

## 4. Roadmap

Cada etapa:
- tem provedor legado/nativo selecionável;
- só vira padrão depois dos portões da seção 6;
- mantém a Preview jogável.

**N0. Fundação** (sem mudança de comportamento)
- Alvos CMake separados (`tools`, `legacy`, `native`, `transitional`).
- Leitura de CDI em alvo próprio, fora do produto, com verificação por mapa do linker e
  strings/imports.
- Relatório de métricas reproduzível (1.1).
- Banco de identificação (seção 3) e manifesto de hardware com dominância.
- Modelo de interoperabilidade (seção 2).
- Inventário de threads, reentradas e eventos (seção 5).
- Custo medido de slots sempre ativos.
- Testes de trace de ordem `(quadro, tarefa, callback, evento)`.

**N1. Arquivos**
- Provedor nativo da API gdFs sobre a leitura de arquivos de `data\` (sem setores).
- Portão: registro normalizado de leituras (arquivo, deslocamento, tamanho, bytes) igual
  ao legado em todos os roteiros de cobertura.
- Ganho: mods de arquivo de qualquer tamanho.

**N2a. Relógio lógico e fila de eventos nativos**, mantendo o adaptador ciclos/IRQ para os
consumidores legados (Maple, render, DMA, GD-ROM).

**N2b. Tarefas**: as threads do jogo sobre o agendador nativo, preservando a ordem
cooperativa observada (protocolo da seção 5).

**N3. Controles**: provedor nativo da API pad/per (teclado, mouse, controles), sem Maple.

**N4. Saves**: provedor nativo da API de backup, com formato lógico dos blocos preservado e
migração versionada.

**N7a. Renderização: IR de comandos de render** (fronteira transitória)
- Captura o que hoje chega ao TA por store queues, DMAC e escrita direta.
- Backend de GPU para essa IR.
- Validação separada de modelos estáticos, geometria dinâmica, uploads de textura e
  paleta, framebuffer direto (FMV/loading), clipping, modificadores, punch-through e
  transparência.
- Resolve o desempenho (hoje ~17 quadros/s em jogo).

**N5. Vídeo**
- Provedor nativo da API Sofdec: decodificação MPEG-1 + ADX com licença revisada (seção 8)
  e saída nativa sincronizada pelo relógio do vídeo, não da AICA.

**N6. Áudio**
- Provedor nativo da API de som do lado SH-4: motor que lê o `BS_SND.MLT` (bancos,
  sequências, PCM/ADPCM) e saída nativa.
- O `MANATEE.DRV` é **analisado estaticamente** (código ARM como documentação de
  comportamento), nunca executado.

**N7b. Renderização nativa**
- Provedores nativos de Ninja/Kamui sobre o renderizador, com assets convertidos pelo
  Builder.
- Só depois que a N7a cobriu os caminhos dinâmicos.

**N2c. Fim da simulação de tempo**
- Depois de N3, N5, N6 e N7b, com prova de que não resta consumidor: remover `BS_POLL`, a
  INTC, a contagem de ciclos e os periféricos do SH-4.

**N8. Executável sem legado**
- Critério operacional (seção 7), verificado por mapa do linker, imports, strings e
  instrumentação.

**N9. Elevação dos sistemas do jogo** (contínua)
- Câmera, movimentação, combate, IA, menus/HUD, inventário... viram módulos nomeados em
  `native/game` com estruturas nativas.
- Validação de contrato por função/sistema contra a versão gerada.
- Código do SDK substituído e morto é **descartado**, não elevado.
- A meta não é "100% das funções": é a ausência de dependência operacional (seção 7).

---

## 5. Threads, reentradas e eventos (protocolo antes de N2b)

As fibras atuais identificam threads por PC de retomada, R15 e cadeia de retornos:
- `rte` pode retomar ou criar uma fibra;
- `longjmp` depende de 4 pontos de reentrada gerados (`sh4_threads.cpp`);
- a fila de IRQ reentrega fontes até o reconhecimento (`sh4_intc.cpp`).

Antes de trocar:
- inventariar entradas de thread, yields, reentradas e fontes de evento;
- documentar prioridade, mascaramento, reconhecimento, reentrância e eventos simultâneos;
- testes de trace com a ordem observada (não só o estado final).

---

## 6. Portões de validação

- **Contratos normalizados por subsistema**, não RAM inteira (que deixa de ser comparável
  com handles nativos):
  - leituras de arquivo;
  - comandos de render (IR);
  - eventos de áudio (tocar/parar/volume, por banco e canal);
  - snapshots de estado lógico (posição, vida, sala, inventário).
- **Tolerâncias explícitas** (por exemplo, pixels fora da tolerância por quadro, desvio de
  nível de áudio) e **casos negativos** (o portão falha quando deve).
- **Matriz de cobertura**: cada sala, chefe, menu, save/load, cada FMV e o final, com
  roteiros determinísticos.
  - O smoke test atual (1ª área) continua.
  - N8 e N9 exigem a **campanha completa**.
- Suítes atuais como portões permanentes: `test_builder`, `test_preview`, `test_projeto`,
  `test_gameplay`.

## 7. Critério final (operacional)

O executável do produto não tem:
- `Sh4Context`, `sh4_read/write`, despacho por endereço, RAM guest, ciclos/IRQ ou módulo de
  hardware;
- nenhum acesso alcançável às faixas P4, SQ, VRAM, Holly ou AICA.

Além disso:
- o estado persistente está em estruturas nativas;
- renderizador de GPU, áudio, vídeo, controles e saves são nativos;
- o CDI só é usado na conversão;
- o código é recompilável e moddável por módulos nomeados;
- a campanha completa é jogável.

Endereços do Dreamcast podem continuar em ferramentas, mapas de depuração e oráculos, fora
do produto.

## 8. Riscos

| Risco | Mitigação |
|---|---|
| Fronteiras do SDK mal identificadas (sem símbolos; 2 605 funções vindas de ponteiros) | Duas evidências por função; prova de fechamento; provedor legado como reserva |
| Acessos a hardware fora das APIs (439 cargas de SQ em 288 funções, DMAC, framebuffer) | Manifesto de hardware; IR de render na N7a antes de Ninja/Kamui |
| Ordem de tarefas/eventos | Protocolo e testes de trace (seção 5) |
| Estado atravessando guest/nativo (ponteiros de 32 x 64 bits) | Modelo de interoperabilidade antes de N1 |
| Fidelidade visual (autosort, modificadores, fog) | Golden frames com tolerância definida e rasterizador de referência |
| Fidelidade sonora (sequenciamento do MANATEE, DSP) | Análise estática do driver, comparação com o Flycast, testes por banco |
| Semântica de FPU durante a fase híbrida (FSCA/FSRRA/FIPR/FTRV, FPSCR) | Testes de contrato continuam; não verificado ainda bit a bit |
| Desempenho | N7a cedo; custo dos slots medido em N0 |
| Escala da elevação | Por sistema, guiada por valor; o híbrido continua jogável |
| **Legal e licenças** | Assets e gerado privados (já). Também: registro de proveniência por módulo, revisão de licença antes de escolher decoder MPEG/ADX e arquivo de avisos reproduzível. Nenhuma conclusão jurídica é afirmada aqui: **revisão jurídica específica antes de distribuir código elevado ou executáveis** |

## 9. Revisão do Codex da v1 (2026-10-15) e incorporação

| Achado | Onde entrou |
|---|---|
| [ALTA] N2 removia eventos antes dos consumidores | N2 dividida em N2a/N2b/N2c (N2c depois de N3, N5, N6, N7b) |
| [ALTA] API substituída não prova que o hardware fica inalcançável | Manifesto de hardware e prova de fechamento (seção 3); números de SQ/DMAC/Holly/AICA/VRAM em 1.1 |
| [ALTA] Strings de versão não identificam APIs | Duas evidências por função; aliases/wrappers/callbacks registrados (seção 3) |
| [ALTA] Slots não são API nativa nem dão rollback sem recompilar | Três camadas e slots sempre ativos nas fronteiras (seção 2) |
| [ALTA] Classificação "reutilizável" incluía acoplados | Classificação por subcomponente (1.3) |
| [ALTA] N8 e "N9 = 100%" não são critérios certos | Critério operacional (seção 7); SDK morto descartado |
| [MÉDIA] Estado na fase híbrida | Modelo de interoperabilidade (seção 2) |
| [MÉDIA] Semântica de threads | Protocolo e testes de trace (seção 5) |
| [MÉDIA] Portões fracos | Contratos normalizados, tolerâncias, casos negativos, matriz até a campanha (seção 6) |
| [MÉDIA] N7 sem geometria dinâmica, SQ/DMA, framebuffer | N7a com IR de render antes de N7b |
| [MÉDIA] Métricas não reproduzíveis; CDI ligado ao runtime; ADXF | Métricas corrigidas e definidas (1.1); CDI em alvo próprio verificado (N0); ADXF 5.30 e 5.42 |
| [MÉDIA] Legal reduzido a "dados privados" | Proveniência, licenças e revisão jurídica (seção 8) |

## 10. Decisões que dependem do mantenedor

1. **Aprovar o plano** (ou ajustar a ordem). A proposta é começar só pela **N0**, que não
   muda o comportamento.
2. **Backend de GPU**: Direct3D 11 (recomendado para começar, atrás de interface),
   Direct3D 12 ou Vulkan.
3. **Saída de áudio**: XAudio2 (recomendado) ou SDL3.
4. **Decodificador de vídeo**: próprio, biblioteca de licença compatível revisada, ou Media
   Foundation.
5. **Pastas**: `native/`, `legacy/` e `transitional/` como alvos na N0; mover arquivos só
   quando cada subsistema migrar.
