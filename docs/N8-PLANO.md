# N8 — Executável sem legado: plano de execução

**Situação: N8 INICIADO em 2026-10-24** (autorização do mantenedor, com as decisões D-N8-1 a D-N8-4 da seção 9). Ordem
obrigatória N8.0 → N8.8; o andamento de cada subetapa fica na seção 10.
O N2 está encerrado em 100%; o N7 permanece funcional em 95%, com duas pendências que não foram reinvestigadas aqui.
O N2c foi transferido para a primeira subetapa técnica do N8.

Fontes: `CLAUDE.md`, `ARCHITECTURE.md`, `STATUS.md`, `docs/PLANO-PORT-NATIVO.md`, planos/relatórios N0–N7, alvos
CMake, includes entre `app/`, `transitional/`, `native/` e `runtime/`, e o mapa do linker Release atual. Evidências
aprovadas não foram repetidas.

## 1. Limite arquitetural e decisões prévias

### 1.1 Resultado obrigatório

O executável distribuído deve usar somente provedores nativos de arquivos, tempo/eventos, controles, saves, vídeo,
áudio e render. Não pode depender em runtime de GD-ROM/BIOS HLE, Maple/VMU emulado, AICA emulada, PVR/Holly/TA/VRAM
emulados, INTC/TMU/DMAC/CCN/portas/SQ, ciclos nem `BS_POLL`. Esses componentes podem continuar em alvo separado de
validação e nas ferramentas.

Há uma incompatibilidade documental a resolver antes da implementação:

- a seção 7 do plano-mestre também proíbe `Sh4Context`, RAM guest, `sh4_read/write`, despacho e código gerado;
- o roadmap reserva a elevação do gameplay e a eliminação desse substrato para o N10.

**D-N8-1 (bloqueante):** confirmar que N8 elimina hardware simulado e provedores legados do **produto**, mas mantém
temporariamente código gerado, RAM guest, `Sh4Context`, `sh4_ops`, despacho e adapters guest↔nativo até o N10. Sem essa
decisão, “N8 100%” e “N10 elevação” são critérios contraditórios. Recomendação: aceitar esse limite e ajustar a seção 7
do plano-mestre antes de iniciar N8.1.

### 1.2 O que não é legado removível

- `native/render` Vulkan e o futuro backend software da mesma IR;
- SDL3, Vulkan, XAudio2, Media Foundation, Direct2D/DirectWrite, toml++, libogg/libvorbis e dr_libs;
- dados do Builder, saves `.bsv`/VMS e suas migrações;
- Builder, Recompiler, extrator, tradutor, `bs-hwscan`, `bs-diff`, Capstone e Flycast externo;
- Original e câmera Livre, tempo experimental padrão na janela, fallback estável `--tempo nativo` e gravações antigas;
- instrumentação, negativos e referências de regressão fora do produto;
- separação lógica/recursos necessária ao N9/N10.

O renderizador **software é requisito permanente de produto**, não “o legado”. O atual
`runtime/pvr/pvr_render.cpp`, porém, depende de VRAM/TA simulados. N8 deve produzir um backend software de
`native::render::CommandList`; o rasterizador atual fica em validation até a equivalência ser provada.

## 2. Estado verificável do binário

O CMake liga `bluestinger` a `bs_app_core`, `bs_generated`, `bs_transitional`, adapters de vídeo/áudio, `bs_native*` e
`bs_runtime`. O mapa Release contém `sh4_context`, `sh4_runtime`, `sh4_intc`, `sh4_threads`, `memory`,
`sh4_{ccn,dmac,port,sq,tmu}`, `gdrom`, `bios`, `flashrom`, `maple`, `aica_regs`, `pvr_{regs,render,texture,video}`,
`holly_{intc,sb}`, `ta`, `vram`, `time_legacy` e todos os adapters. O produto ainda é híbrido mesmo usando os padrões
nativos.

`bs_runtime_cdi` (`image_source.cpp`, `disc_boot.cpp`) não é ligado ao jogo: serve ao Builder, `bs-diff` e testes. O
produto ainda recebe `bs_disc` transitivamente por `bs_runtime`, embora o portão N0 prove que leitura de CDI não entra
no exe. N8 deve retirar essa aresta do produto sem retirar `bs_disc` das ferramentas.

## 3. Inventário de dependências e destino

“Evidência” é prova já existente que pode ser reutilizada; cada corte ainda exige catraca estática e teste dirigido.

| ID | Caminho / símbolos | Finalidade e dependentes | Substituto / evidência | Ação | Risco | Aceitação |
|---|---|---|---|---|---|---|
| L1 | `cpu/sh4_context.*`, `sh4_ops.h`, `sh4_gen.h`; `generated/*` | estado/semântica AOT; gameplay, adapters e `GameHost` | elevação só no N10 | manter temporariamente, sujeito a D-N8-1 | alto | catraca só permite esse substrato; campanha/determinismo |
| L2 | `cpu/sh4_runtime.*`: dispatch, memória, wait/service | indiretas, RAM e serviço | fronteiras nativas | migrar parcialmente; manter dispatch/RAM até N10 | alto | nenhum dispatch alcança MMIO; negativo da catraca |
| L3 | `cpu/sh4_threads.*`: fibras, `rte`, `longjmp` | tarefas/callbacks gerados | ordem lógica N2; falta bridge direto | manter até N10; retirar IRQ em N8.1 | alto | trace de ordem, pausa, LOAD, sessões N2 |
| L4 | `time_legacy`, `time_hooks`, `time_adapter`, `cri_idle`; `cycles`, `BS_POLL` | três modos de tempo | `LogicalClock`, `EventQueue`, `ProgressMonitor`; U1–U8/H9 | migrar N8.1; compatibilidade antiga no runner validation | muito alto | 15.411 passos, LOAD, saves, 6/6 pausa, negativos |
| L5 | `cpu/sh4_intc.*` | prioridade/mascaramento e entrega de IRQ à CPU | fila nativa; falta ponte a callbacks guest | remover do produto em N8.1 | muito alto | trace evento/tarefa/callback e simultâneos |
| L6 | `memory/sh4_{tmu,ccn,port}.*` | temporizadores e periféricos SH-4 temporais | tempo/eventos/operações nativas | remover no N8.1 após manifesto zerado | alto | hwscan, TMU e campanha |
| L7 | `memory/memory.cpp`, `sh4_mem.h` | RAM guest + roteamento MMIO/VRAM/AICA | RAM guest até N10 | migrar a RAM-only; retirar hardware | alto | nenhuma faixa P4/MMIO/VRAM/AICA alcançável |
| L8 | `sh4_{dmac,sq}`, `pvr/{holly_intc,holly_sb,ta,vram,pvr_regs}` | transporte/eventos das listas Kamui/Ninja, textura/framebuffer | IR existe; captura ainda lê TA/VRAM | em N8.1 retirar só função temporal; remover fisicamente em N8.7 | muito alto | SQ/DMAC/Holly zerados; classes gráficas equivalentes |
| L9 | `pvr_{render,texture,video}` | software, decode e quadro | futuro software-IR; hoje fallback de textura | preservar como referência; retirar só depois do software-IR | muito alto | software-IR = atual; Vulkan = software-IR |
| L10 | `native/render/vulkan/*` | backend principal | nativo; N7 aprovado nas GPUs cobertas | preservar permanentemente | AMD aberto | NVIDIA/Validation/fallback preservados |
| L11 | `transitional/render_capture.*` | TA/VRAM/PVR → `CommandList` | sem produtor nativo independente | migrar N8.7; depois validation | muito alto | IR nativa pareada, inclusive fog/volumes |
| L12 | `gdrom/*`, `hle/{bios,flashrom}`, `boot/native_boot` | boot, syscalls, drive; `GameHost`/service | `DataFileSystem`, saves e boot por dados; N0/N1/N4 | migrar/remover N8.3; preservar oráculo | alto | boot/leitura/RTC/save sem HLE |
| L13 | `image_source`, `disc_boot`, `bs_runtime_cdi`, `bs_disc` | CDI→dados, Builder, diff/testes | não deve ter substituto no jogo | preservar fora do produto | baixo | jogo sem CDI; Builder passa |
| L14 | `maple/*`, `input_bridge` | controle/VMU legado e DMA | `InputSystem`/`pad_adapter`; `bu_*`; N3/N4 | remover N8.4; preservar validation | alto | controles, hotplug, SAVE/LOAD, VMS |
| L15 | `pad_adapter`, `pad_contract` | ABI pd nativa sobre RAM guest | contrato N3 | manter até N10; desacoplar Maple | médio | `test_pd_nativo`, controles, campanha |
| L16 | `bu_adapter`, `bu_machine` | ABI backup nativa | `ISaveStore`; VMS bilateral | manter até N10 | médio | saves antigos/import/export/SAVE/LOAD |
| L17 | Sofdec/CRI gerado, `--video legado`, YUV PVR | fallback de filmes | `video_adapter`/`native/video`; N5, 17 IDs | retirar do produto N8.5; preservar oráculo | alto | E001, matriz, START, legenda/sincronia |
| L18 | `aica_regs`, SDK/Manatee gerado, `--audio legado` | fallback de áudio/AICA | `sound_adapter`, `native/audio`; H1–H9 | retirar do produto N8.6; preservar oráculo | alto | bateria H, Flycast, DSP, zero AICA |
| L19 | adapters, `guest_abi`, `boundary_registry`, `free_camera` | marshaling guest↔nativo | chamadores só serão elevados no N10 | manter adapters nativos; retirar seletor legado | médio | apenas provider nativo; Original/Livre |
| L20 | `runtime/debug/*`, `pad_probe`, `BS_WRITE_WATCH` | diagnóstico/manifesto/negativos | não é runtime necessário | preservar no validation, retirar do produto | médio | evidência continua disponível; mapa limpo |
| L21 | `runtime/mods/mod_api`, `BS_MODS`, slots | projeto editável e fronteiras | N10 amplia, mas uso atual é válido | preservar; desacoplar hardware | médio | projeto/mods compilam; slots nativos |
| L22 | `bs_legacy` INTERFACE e `bs_runtime` monolítico | agrega todo runtime | falta separação real | dividir em produto, substrato AOT e validation | alto | grafo/mapa provam separação |

### 3.1 Opções e dependências de build

| Item | Destino |
|---|---|
| opções `legado` de arquivos/controles/saves/vídeo/áudio/tempo | sair do produto; permanecer só em validation enquanto úteis |
| `--tempo experimental` | caminho normal da janela; nome pode ficar temporariamente por compatibilidade CLI |
| `--tempo nativo` | fallback estável/sessões antigas; continua por ciclos até o N8.2 (D-N8.1-A, mantenedor 2026-10-24), quando o estável por ciclos vai para o alvo de validação; no produto o tempo lógico já não usa ciclos desde o N8.1 |
| `--render software` | preservar permanentemente como backend da IR nativa |
| `BS_BUILD_TOOLS`, `BS_BUILD_TESTS`, `BS_WRITE_WATCH`, `BS_HW_PROBE` | preservar; diagnósticos fora do produto Release |
| `BS_MODS`/`BS_MODS_DIR` | preservar para projeto editável/N10 |
| `bs_disc`, `bs_runtime_cdi`, tradutor, hwscan, Builder/Recompiler | preservar fora do produto |
| xinput, SDL3, Vulkan, toml++, backends Windows e codecs | preservar: são dependências nativas |

## 4. Efeito das pendências do N7

Não há autorização para corrigi-las neste planejamento.

1. **AMD `VK_ERROR_DEVICE_LOST` / GPU fault `Invalid Read`.** Não permite declarar Vulkan universalmente estável na
   AMD, mas não impede planejar N8 porque software continua fallback real. Impede remover o fallback ou usar só NVIDIA
   como prova. O corte PVR só ocorre depois do backend software-IR. A causa continua no N7; recuperação não é correção.
2. **Fog por tabela e modifier volumes.** Há uso real desde o render 1127, mas falta implementação fiel e quadro
   pareado com Flycast. `render_capture`, raster legado e Flycast permanecem até a prova suficiente. N8.7 não pode
   retirar o transporte PVR antes disso; etapas não gráficas podem avançar sem declarar N8 concluído.

**Portão entre N7 e N8:** as duas pendências do N7 não bloqueiam N8.0 nem N8.1–N8.6, desde que fallback, captura,
raster legado e Flycast sejam preservados. Fog/volumes bloqueiam o corte gráfico N8.7. A falha AMD não bloqueia N8.1
e não autoriza remover o fallback; N7 continua em 95% até sua validação própria, independentemente do progresso N8.

## 5. N2c como primeira subetapa técnica

### N8.1 — N2c: fim da simulação de tempo

1. **Objetivo/componentes:** retirar `BS_POLL` como motor, `Sh4Context::cycles`, `time_legacy`, `sh4_intc`,
   `sh4_tmu`, `sh4_ccn` e `sh4_port`; entregar eventos via `LogicalClock`/`EventQueue` às tarefas/callbacks gerados.
2. **Pré-requisitos:** D-N8-1; N2 U1–U8 (atendido); snapshot N8.0; manifesto de consumidores; protocolo explícito de
   prioridade, mascaramento, reconhecimento e reentrância sem INTC.
3. **Alterações previstas:** bridge transitório de eventos; tempo estável sem ciclos; remover polls gerados e acessos
   TMU/CCN/portas; eliminar sincronização do pad pelo DMA Maple. Em N8.1, `sh4_dmac`, `sh4_sq`, `holly_intc/sb`, PVR,
   Maple e GD-ROM perdem qualquer papel de relógio/agendamento, mas seus transportes permanecem isolados até N8.3,
   N8.4 e N8.7. A retirada física de `sh4_dmac`/`sh4_sq` pertence ao corte gráfico N8.7.
4. **Riscos/reversão:** deadlock, ordem e LOAD. Um commit por consumidor; validation conserva o caminho anterior;
   reversão por commit, sem reescrever histórico.
5. **Testes:** sessões N2 nos dois modos, `test_assistido`, `test_tempo`, pausa/retomada, LOAD, saves, trace de ordem,
   mutações e rota ampliada.
6. **Evidências:** 15.411 passos estáveis sem diferença; pós-LOAD limitado à lista fechada, reconvergência/saves iguais;
   manifesto sem ciclos/IRQ e negativos falhando.
7. **Conclusão:** produto sem `BS_POLL`, `cycles`, `time_legacy`, `sh4_intc`, TMU, CCN ou portas alcançáveis; DMAC,
   SQ e módulos dos cortes posteriores não medem nem agendam tempo; fallback estável e gravações antigas continuam.
8. **Preview/Builder:** reconstruir/testar ambos; Builder deve gerar o mesmo comportamento.

N8.1 é a primeira alteração funcional. N8.0 abaixo só captura referências e nada remove.

## 6. Subetapas executáveis

### N8.0 — Portão e referências (sem mudança funcional)

1. Objetivo: congelar proveniência, rotas, hashes e resultados.
2. Pré-requisitos: autorização para iniciar N8 e D-N8-1. D-N8-2 só é necessária antes do corte N8.7; a captura de
   referências é neutra quanto à futura fonte da IR.
3. Alterações: só scripts/manifestos se indispensáveis; nenhuma rota de produto.
4. Risco/reversão: referência incompleta; repetir captura, sem tocar no binário aprovado.
5. Testes: regressão integral nível 3 pré-corte, campanha reproduzível, Builder/Preview e oráculos.
6. Evidências: pacote local em `analysis/`; matriz sala/chefe/menu/SAVE/LOAD/FMV/final.
7. Conclusão: referências independentes e mutações; nenhum legado exclusivo sem oráculo.
8. Dist: não muda; Preview anterior preservada.

### N8.2 — Alvos e catraca do produto

1. Objetivo: separar substrato AOT, runtime nativo e simulação validation.
2. Pré-requisitos: N8.1.
3. Alterações: desmembrar `bs_runtime`; alvo validation; remover arestas acidentais (`bs_disc`, `dcmap`, sondas);
   ampliar mapa/imports/strings/hwscan.
4. Risco/reversão: objeto necessário omitido; commits só de grafo.
5. Testes: builds produto/validation/tools, N0 sem CDI/nativo limpo, negativos.
6. Evidências: objetos por alvo e mapa sem caminhos privados.
7. Conclusão: produto não liga simulação; exceções L1–L3/L7/L19 enumeradas.
8. Dist: Builder exporta novos alvos; Preview após build dirigido.

### N8.3 — Boot, arquivos, GD-ROM, BIOS e flash

1. Objetivo: retirar providers de arquivos legado, drive e HLE do produto.
2. Pré-requisitos: N8.2; inventário de syscalls e contrato do boot.
3. Alterações: boot tipado, provider único, persistência nativa; HLE em validation.
4. Risco/reversão: boot tardio, leitura, RTC/região; corte por família.
5. Testes: N1, boot, mods, RTC, saves/import/export e campanha.
6. Evidências: zero setores/syscalls no produto e bytes iguais.
7. Conclusão: sem objeto GD-ROM/BIOS/flash alcançável.
8. Dist: Preview/Builder; CDI continua na conversão.

### N8.4 — Controles, saves e Maple

1. Objetivo: retirar Maple/VMU simulada, `input_bridge` e flags legado.
2. Pré-requisitos: N8.1 sem DMA; N8.3 com persistência independente.
3. Alterações: providers únicos; adapters pd/bu ainda guest↔nativo; Maple em validation.
4. Risco/reversão: amostra, hotplug, SAVE/LOAD; controles/saves em commits distintos.
5. Testes: pd, controles, saves, VMS bilateral, LOAD e campanha.
6. Evidências: inputs/estado/saves iguais, zero Maple.
7. Conclusão: produto sem Maple/VMU e sem opção legado.
8. Dist: Preview/Builder; saves locais nunca substituídos.

### N8.5 — Filmes e CRI legado

1. Objetivo: remover provider Sofdec/YUV legado.
2. Pré-requisitos: N8.3; catálogo/conversões 17 preservados.
3. Alterações: provider nativo único; testes históricos em validation/oráculo.
4. Risco/reversão: duração, START, fim, legenda/áudio; corte por slots MW.
5. Testes: E001, matriz 17, START, cores, legendas, cenas e mods.
6. Evidências: eventos/duração Flycast; nenhuma leitura SFD em runtime.
7. Conclusão: sem Sofdec alcançável nem YUV dependente do PVR.
8. Dist: Builder converte; Preview só com dados locais.

### N8.6 — Áudio e AICA legados

1. Objetivo: remover SDK/Manatee gerados, MMIO/RAM AICA e fallback legado.
2. Pré-requisitos: N8.1/N8.2; H1–H9; oráculo preservado.
3. Alterações: provider nativo único; AICA/driver em validation/análise.
4. Risco/reversão: sequenciamento, fades, DSP/LOAD; três categorias em commits.
5. Testes: H dirigido, Flycast, dublagem/legenda, pausa, LOAD, underrun.
6. Evidências: eventos/áudio nas tolerâncias; manifesto AICA zerado.
7. Conclusão: nenhum AICA/ARM7/driver no produto.
8. Dist: Preview/Builder; bancos derivados do CDI.

### N8.7 — IR nativa, fallback software e retirada do PVR

1. Objetivo: produzir `CommandList` sem SQ/DMAC/TA/VRAM/PVR; Vulkan + software na mesma IR.
2. Pré-requisitos: N8.2, D-N8-2, evidência N7 de fog/volumes e software-IR aprovado.
3. Alterações: API Kamui/Ninja ou listas em buffer nativo; texturas/framebuffer nativos; backend software-IR; captura
   antiga só em validation.
4. Risco/reversão: maior risco; migrar por classe, comparação tripla.
5. Testes: estático/dinâmico, punch-through, autosort, paleta, framebuffer/YUV, clipping, fog, volumes, câmeras,
   Vulkan/software e Validation Layers.
6. Evidências: quadros/tolerâncias contra software/Flycast; SQ/DMAC/VRAM/PVR zerados.
7. Conclusão: sem PVR/Holly/TA/VRAM/SQ; dois backends funcionais. AMD continua reportado se aberto.
8. Dist: atualização obrigatória; Preview nos dois backends; Builder exporta ambos.

### N8.8 — Fechamento e distribuição

1. Objetivo: provar ausência de legado obrigatório e de regressão.
2. Pré-requisitos: N8.1–N8.7 e bloqueios N7 relevantes resolvidos ou decisão explícita compatível.
3. Alterações: docs, catracas e últimos vínculos já mortos.
4. Risco/reversão: cobertura; não apagar oráculos, voltar à Preview aprovada.
5. Testes: regressão nível 3 pós-corte, campanha, Preview, Builder/Recompiler e projeto editável.
6. Evidências: mapa/imports/strings/hwscan, matriz, hashes de saves e produtos.
7. Conclusão: seção 7 aprovada; revisão Codex sem achado funcional aberto.
8. Dist: Preview/Builder finais x64 testados do pacote.

## 7. Critérios de aceitação final

### 7.1 Bloqueantes para N8 = 100%

- D-N8-1 resolvida e plano-mestre coerente com N10;
- N2c: sem ciclos, `BS_POLL`, INTC ou periférico SH-4 temporal no produto;
- todos os providers do produto nativos; nenhuma opção legado necessária;
- nenhum GD-ROM/BIOS/Maple/AICA/PVR/Holly/TA/VRAM/SQ alcançável;
- Vulkan e fallback software sobre IR nativa;
- controles, câmeras, menus, saves antigos, SAVE/LOAD, filmes, áudio, legendas e pausa sem regressão;
- tempo padrão/fallback e gravações antigas preservados sem ciclos;
- determinismo/saves nas rotas e campanha completa reproduzível;
- Builder/Recompiler, Preview e projeto editável; CDI só na conversão;
- mapa/imports/strings/hwscan e instrumentação concordam;
- regressões nível 3 pré/pós, negativos e revisão independente aprovados;
- Original e futura Remastered compartilham o núcleo, sem fork.

### 7.2 Não bloqueantes; N9

SSAO, 16:9 real, MSAA, iluminação/sombras/reflexos, assets remasterizados, novos presets, HUD e otimizações opcionais.
Fog e modifier volumes não são remasterização: a fidelidade deles é N7/N8 bloqueante para retirar a referência PVR.

## 8. Riscos e recuperação

| Risco | Prevenção | Recuperação |
|---|---|---|
| referência removida | validation + evidência externa antes do corte | reativar só provider da subetapa |
| ordem temporal | trace, LOAD/replays, negativos | voltar ao bridge anterior em commit isolado |
| MMIO oculto | dominância + sonda + campanha | bloquear corte e ampliar fronteira comprovada |
| save incompatível | migração não destrutiva, cópia/hashes | ler antigo; restaurar cópia |
| Builder incompleto | allowlist e teste do pacote | conservar kit anterior/reverter CMake |
| regressão Vulkan/AMD | software-IR e Validation Layers | fallback explícito; manter N7 aberto |
| fog/volumes sem prova | Flycast + raster legado | adiar N8.7, não etapas não gráficas |
| invasão N9/N10 | catraca de escopo e revisão | reverter mudança fora de escopo |

Cada subetapa terá commits pequenos, sem apagar histórico. Preview anterior, saves, mods, `known_versions.h`, backups,
stashes e worktrees são intocáveis. Fonte legada só sai fisicamente depois de preservada no validation e confirmada
como desnecessária às ferramentas.

## 9. Decisões do mantenedor antes da implementação

- **D-N8-1 (bloqueante):** manter substrato AOT/RAM guest até N10 ou antecipar elevação. Recomendado: manter e corrigir
  o critério do plano-mestre.
- **D-N8-2 (bloqueante para N8.7):** fonte da IR: (a) APIs Kamui/Ninja nativas ou (b) listas Kamui em buffer nativo,
  sem SQ/DMAC/VRAM. Recomendado: (b) no N8; interfaces elevadas no N10.
- **D-N8-3:** validation legado junto do build dev ou opção separada. Recomendado: opção separada, nunca na dist final.
- **D-N8-4:** campanha completa como portão de fechamento, não de início. Recomendado: N8.0 captura o disponível; a
  matriz cresce por subetapa e é completa em N8.8.

**Decididas pelo mantenedor em 2026-10-24** (autorização do N8):
1. **D-N8-1:** o N8 remove a simulação de hardware do Dreamcast e os provedores legados do produto; o substrato AOT
   (código gerado, RAM guest, `Sh4Context`, `sh4_ops`, despacho e fibras) fica até o N10. A seção 7 do plano-mestre foi
   ajustada.
2. **D-N8-2:** a IR gráfica vem preferencialmente das **listas do Kamui em buffers nativos**, com interfaces
   desacopladas (as APIs Kamui/Ninja nativas ficam para o N10).
3. **D-N8-3:** o alvo legado de diagnóstico e validação fica **separado** e nunca entra na distribuição final.
4. **D-N8-4:** a campanha completa é critério obrigatório de **encerramento** do N8.8, não pré-requisito do N8.0.

O renderizador em software é preservado permanentemente como recuo do Vulkan.

## 10. Andamento

| Subetapa | Situação |
|---|---|
| N8.0 | **concluída em 2026-10-25**: regressão nível 3, correções (SPIR-V, ícone, guardas das suítes), as 16 referências capturadas, validadas e reproduzidas uma a uma (0 diferenças), com negativos (`docs/N8-0-REFERENCIAS.md` seção 8); fixação do áudio legado nas suítes históricas decidida pelo mantenedor (opção b), com as suítes aprovadas (K12 2/2, `test_cri` 33/33, `test_efeito` 17/17, `test_n2b2` 17/17, `test_n2b` 52/52), com a cobertura nativa mapeada (`STATUS.md`) |
| N8.1 | **concluída em 2026-10-26** (`docs/N8-1-PREPARACAO.md` seção 7): S2n/S4n; `BS_INTC_TRACE`; cadência B (179 exceções de SPC aprovadas e conferidas uma a uma); TMU, CCN não gráfico, cabo e ponte de interrupções no tempo lógico por interceptação localizada (16/16 sem exceções novas, 0 fora do contrato, independente do ruído de ciclos). O estável por ciclos e os modelos ficam até o N8.2 (D-N8.1-A/B) |
| N8.2 | **concluída em 2026-10-26** (`docs/N8-2-SEPARACAO.md`): runtime por papéis (bibliotecas de objetos), configuração `BS_VALIDACAO` (preset `msvc-validacao`; produto com stubs que recusam as mutações e sondas de teste), `bs_disc_data` × `bs_disc`, catraca `n8_produto_papeis` (manifesto completo × mapa do linker). Estável e instrumentação compartilhados (mantenedor); simulação compartilhada até N8.3–N8.7 |
| N8.3 | **concluída em 2026-10-26** (`docs/N8-3-BOOT.md`): serviço nativo das syscalls (`system/syscall_service`, contrato medido do jogo, fora dele erro explícito); BIOS, drive de GD-ROM e flash HLE fora do produto (`bs_runtime_hle` só na validação e nas ferramentas); `--arquivos legado`, `--flash` e `BS_GDROM_TIMING` só na validação; registradores do drive e G1 como erro explícito; 16/16 no produto e na validação com o modelo |
| N8.4 | **concluída em 2026-10-27** (`docs/N8-4-PERIFERICOS.md`): barramento de periféricos sem dispositivos (`system/maple_bus`, mesmo efeito do modelo sem controle; fora do contrato erro explícito); modelo do Maple, `input_bridge::install`, `--controles legado`/`--saves legado` só na validação; soft reset encerra a sessão pelo menu do sistema; 16/16 |
| N8.5 | **concluída em 2026-10-27** (`docs/N8-5-FILMES.md`): `--video legado` e o conversor YUV do TA fora do produto (erro explícito); Sofdec só com a init (AOT), entradas MW sempre nativas, 0 SFD; suítes da linha do tempo do Sofdec na validação; `test_gameplay` no filme nativo; 16/16 |
| N8.6 | **concluída em 2026-10-27** (`docs/N8-6-AUDIO.md`): área de som nativa (`system/sound_bus`: RAM de som, controle e RTC com o contrato medido; fora dele erro explícito); modelo da AICA, `--audio legado` e `BS_AICA_*` só na validação; partida do DMA do G2 é erro no produto; 16/16. H4 com 3 falhas anteriores ao N8.6 (critério desatualizado desde o `e13a123`), sem alterar o critério |
| N8.7–N8.8 | não iniciadas |
