# N8.2: separação do produto nativo e do ambiente de validação

**Situação (2026-10-26):** autorizado pelo mantenedor, só o N8.2. Referência imutável do N8.1: o commit `3f203f5`.

Decisões do mantenedor (2026-10-26):
- o tempo estável `--tempo nativo` fica **nos dois** (produto e validação), compartilhado: a CLI e o headless histórico
  não mudam;
- a instrumentação das referências fica **no produto**, compartilhada e inerte sem a variável. As 16 referências
  continuam conferindo o próprio exe do produto;
- só as mutações e sondas exclusivas de teste saem do produto.

D-N8-3 (2026-10-24): o alvo de validação é separado e nunca entra na distribuição.

## 1. Inventário (grafo de dependências)

**Executável do jogo:**

```
bluestinger -> bs_app_core -> bs_runtime, bs_transitional, adapters de vídeo/áudio, bs_native_*
            -> bs_generated -> bs_runtime
```

**Builder e ferramentas:**

```
bs_builder_core -> bs_recomp_lib (gerador), bs_runtime, bs_runtime_cdi -> bs_disc (CDI)
bs-diff -> bs_generated, bs_runtime_cdi        bs-extract -> bs_disc
```

### 1.1 Classificação

| Grupo (papel) | Módulos | Classe |
|---|---|---|
| substrato AOT (`bs_rt_aot`) | `sh4_context`, `sh4_runtime` (despacho, serviço, cadência B), `sh4_threads`, `memory` (RAM e roteamento), `mods/mod_api` | necessário ao produto (L1–L3, L7, L21), até o N10 |
| serviços nativos (`bs_rt_nativo`) | `irq_bridge`, `tmu_logical`, `sys_logical` | necessário ao produto (N8.1) |
| simulação no caminho do produto (`bs_rt_simulacao`) | `gdrom`, `data_source`, `bios`, `flashrom`, `native_boot`, `maple`, `input`, `aica_regs`, `pvr_*`, `holly_*`, `ta`, `vram`, `sh4_sq`, `sh4_dmac` | compartilhado temporariamente; candidato a remoção nos cortes N8.3–N8.7 (não tocado) |
| tempo estável e modelos de recuo (`bs_rt_estavel`) | `time_legacy`, `sh4_intc`, `sh4_tmu`, `sh4_ccn`, `sh4_port` | compartilhado temporariamente: D-N8.1-A, CLI e headless históricos, recuo dos serviços do N8.1 fora do contrato |
| instrumentação das referências (`bs_rt_diagnostico`) | `event_trace`, `intc_trace`, `clock_log`, `file_log`, `call_trace`, `snapshot` | compartilhado (decisão do mantenedor): inerte sem a variável |
| exclusivo de validação (`bs_rt_validacao`) | `hw_probe` (sonda do N0), `cycle_noise` (`BS_CICLOS_RUIDO`), `cri_diag` (`BS_CRI_DIAG`) | exclusivo de validação: só com `BS_VALIDACAO` |
| dados do jogo (`bs_disc_data`) | `RuntimeDisc` e conferência dos dados, `IP.BIN`, descrambler, ISO9660 sobre a interface genérica, hash | necessário ao produto |
| leitura de imagens (`bs_disc`) | leitor de CDI (`cdi_image`), extrator (`extract`) | exclusivo das ferramentas (Builder, `bs-extract`, `bs-diff`, testes) |
| `bs_runtime_cdi` | `image_source`, `disc_boot` | exclusivo das ferramentas (já era) |
| `bs_dcmap` | mapa de regiões (enum de subsistema do registro de fronteiras) | compartilhado (metadado) |
| gerador, tradutor, Builder, `bs-hwscan`, `bs-diff` | `translator/`, `builder/`, `tools/` | fora do produto (ferramentas) |
| ponte guest↔nativo | `transitional/*`: adapters, `guest_abi`, `boundary_registry`, `free_camera` | necessário ao produto (L19), até o N10 |
| nativo permanente | `native/*` (Vulkan e software, áudio, vídeo, localização, plataforma) | necessário ao produto |

Reservado ao N10: elevação do substrato AOT (código gerado, RAM guest, despacho e fibras).

Nenhum módulo foi classificado como removível só por não ser acessado nas referências. A simulação e os modelos de
recuo continuam ligados ao produto, onde a dependência é real.

## 2. Arquitetura anterior e nova

**Antes:**
- `bs_runtime` era uma biblioteca estática monolítica com todos os papéis, inclusive as mutações e sondas de teste;
- o `bs_disc` inteiro (com o leitor de CDI e o extrator) era dependência pública do runtime;
- as variantes de investigação (`msvc-release-mods`, `msvc-release-watch`) eram pastas de build sem configuração formal.

**Agora:**
- **runtime por papel:** `bs_runtime` é montado de bibliotecas de objetos por papel (`runtime/CMakeLists.txt`), com
  os mesmos objetos no produto. O grafo vai para `runtime_papeis.txt` no build;
- **configuração de validação explícita:** opção `BS_VALIDACAO` e preset `msvc-validacao` (Release). O Debug
  (`msvc-debug`, ctest) também é de validação;
  - sem ela (o `msvc-release` e o Builder), o lugar das mutações e sondas é de `runtime/debug/validacao_stubs.cpp`:
    sem efeito, e a variável correspondente é recusada (erro, saída 2). Um teste nunca passa no vazio com o
    executável errado;
  - `BS_WRITE_WATCH` exige `BS_VALIDACAO`; as pastas `mods` e `watch` são configuradas com ela;
- **dados do jogo separados das imagens:** `bs_disc_data` (produto) e `bs_disc` (leitor de CDI e extrator, ferramentas).
  O executável do jogo não liga o leitor de CDI;
- **sem dependência circular:** o produto não depende de nenhuma ferramenta. A validação é o mesmo código com os
  módulos de teste.

### 2.1 Catraca

**`n8_produto_papeis` (ctest, `tests/n8/check_papeis.cmake`):**
1. os papéis do runtime batem com a configuração;
2. o mapa do linker do `bluestinger.exe` confirma os objetos:
   - sem `BS_VALIDACAO`: nenhum `hw_probe`/`cycle_noise`/`cri_diag`, e o stub presente; com ela, o contrário;
   - nunca o leitor de CDI, o extrator, `image_source` nem `disc_boot`;
3. o produto recusa `BS_CICLOS_RUIDO` (saída 2); a validação aceita;
4. o manifesto completo (`runtime_manifesto.tsv`, papel e fonte de cada objeto, gerado das mesmas listas dos alvos)
   (revisão Codex):
   - todo objeto dos papéis da configuração está no mapa (38 no produto, 40 na validação);
   - a única exceção, numa lista fechada, é `snapshot`: diagnóstico sem chamador no executável, que o linker não puxa;
   - nenhum papel fora da configuração;
   - o mapa não é anterior ao executável.

   Os negativos reprovam: um objeto ausente e um mapa de outro link.

Os negativos cruzados (o mapa do produto conferido como validação e o contrário) reprovam.

**`test_preview`:** a Preview recusa `BS_CICLOS_RUIDO`, `BS_HW_PROBE` e `BS_CRI_DIAG`.

`n0_produto_sem_cdi` continua.

## 3. Uso nas provas

| Prova | Executável |
|---|---|
| as 16 referências, S2n, `test_n2b2`, `test_assistido` | produto (`msvc-release`, a Preview) |
| ruído de ciclos (`BS_CICLOS_RUIDO`) | só o build de validação (`msvc-validacao`) |
| diagnóstico do CRI (`BS_CRI_DIAG`: `test_cri`, `test_efeito`, S4 do `test_n2b`, S4n) | builds `mods`/`watch` (validação) |
| sonda do N0 (`BS_HW_PROBE`) | só o build de validação |

## 4. Limitações herdadas do N8.1, depois da separação

| Limitação | Representação |
|---|---|
| D-N8.1-A: o estável por ciclos e as gravações históricas | papel `estavel`, compartilhado (produto e validação), por decisão do mantenedor. A cadência por ciclos fica restrita a esse modo (`cycle_cadence()`); o tempo lógico não a usa |
| a leitura do TMU no código do guest | continua no código gerado. O produto a atende pelo serviço nativo (`nativo`), e o modelo (`estavel`) só fora do contrato. Sai com o gerador (depois da equivalência) |
| B11/E6 (D7 com o TMU parado, áudio nativo) | só existe com `BS_TMU_DIAG` no tempo estável. O diagnóstico é do modelo (`estavel`) e continua no produto e na validação, como antes. Não resolvido (o relógio do estável não muda sem equivalência e aprovação) |
