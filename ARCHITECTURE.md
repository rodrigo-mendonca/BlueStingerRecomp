# Arquitetura — Blue Stinger Recompiled

Status: **aprovada (M0)**. As decisões estão na §8.

---

## 0. O que já sabemos do disco

Levantado lendo a imagem fornecida pelo usuário (`ISO/Blue Stinger.cdi`, que
não é versionada). Nenhum byte do jogo foi copiado para o repositório.

| Item | Valor observado | Consequência |
|---|---|---|
| Formato da imagem | DiscJuggler CDI v3.5 (trailer `0x80000006`), multissessão | O extrator precisa ler a tabela de faixas do CDI. Não basta procurar a LBA 45000. |
| Setor da faixa de dados | 2336 bytes (Mode 2 Form 1, subheader de 8 bytes + 2048 de dados) | Leitor de setor parametrizado por faixa (2048/2336/2352). |
| LBA inicial da faixa de dados | 239355 (IP.BIN no setor 0 da faixa) | Rip "self-boot": o layout não é o do GD original. |
| IP.BIN | `SEGA SEGAKATANA`, produto **T13001N**, **V1.000**, data 1999-07-05, área `JUE`, periféricos `0799A10` | Versão **NTSC-U 1.000**. O bit 0 dos periféricos é 0, então **Katana (não Windows CE)**. |
| Executável | `1ST_READ.BIN`, 1 975 556 bytes, **não embaralhado** (começa com `nop`s e código SH-4 válido) | O desembaralhador (scramble do MIL-CD) só roda se a heurística detectar embaralhamento. |
| Volume | `BLUE_DCRES`, 2094 entradas, quase tudo em `BSDATA/` | |
| Arquivos com LBA < 239355 | `*.SFD` (FMV), p.ex. `ENDING.SFD` LBA 25, `NEWGAME.SFD` LBA 58771 | Os vídeos ficam em **outra faixa/sessão** do CDI. O extrator resolve LBA → (faixa, offset). |
| Tipos de arquivo | 790 `.NT` + 759 `.NJ` (Ninja: texturas/modelos), 266 `.BIN` (salas `R??nn`), 233 `.WAV`, 16 `.SFD` (Sofdec), `.MLT`/`.MPB`/`.MSB` (som Manatee), `MANATEE.DRV` | O jogo usa o SDK Katana com **Ninja + Kamui2**, o driver de som **Manatee** (ARM7) e vídeo **Sofdec** (MPEG-1 + ADX). |
| `R*.BIN` | Cabeçalho de 9 offsets (o primeiro sempre 0x24). Nenhum dos 265 arquivos tem epílogo de função (`lds.l @r15+,pr` + `rts`); o `1ST_READ.BIN` tem ~1/KB | **São dados de sala, não overlays** (R3 reduzido, ver §9). |

O SH-4 do Dreamcast roda em **little-endian**, igual ao x64. Isso elimina a troca
de bytes em todos os acessos à memória, uma vantagem real sobre os projetos
N64 e Xbox 360.

---

## 1. Lições das referências

| Projeto | Licença | O que adotamos | O que evitamos |
|---|---|---|---|
| N64Recomp | MIT | Config TOML por jogo. Uma função C por função guest. `LOOKUP_FUNC` para chamadas indiretas. Funções "patch" que substituem as geradas na linkagem. Saída em C puro sem dependência do runtime. | Depender de ELF com símbolos (não temos). |
| N64ModernRuntime | **GPL-3.0** | A separação conceitual entre runtime genérico (`ultramodern`) e ponte para o código gerado (`librecomp`), com I/O de plataforma via callbacks. | **Copiar código.** Só a ideia. |
| XenonRecomp | MIT | Analisador separado do gerador (`XenonAnalyse` → TOML de tabelas de salto). *Mid-asm hooks* declarados no TOML. Fronteiras de função explícitas como escape. Tabela hash de despacho. | Heurística de tabela de salto específica de um jogo, sem fallback. |
| XenosRecomp | MIT | Organização do renderer: constantes por push/root, texturas bindless, cache de pipelines por hash XXH3. | — (o PVR2 não tem shaders programáveis, então só a organização do runtime serve). |
| Initial D AS3 SH-4 Recomp | **sem licença declarada visível** (ver LEGAL.md dele) | Layout `translator/ runtime/ tools/ tests/ docs/`. Launcher que valida e extrai o disco do usuário. Política de rejeitar alvos nulos/ímpares/fora da imagem. **Auditoria de delay slots** (o projeto teve 67 bugs de `rts` com delay slot perdido). Lógica a 60 Hz desacoplada da taxa de apresentação. | Ler ou copiar código: sem licença, é "todos os direitos reservados". |
| Flycast | **GPL-2.0** | Referência de comportamento (lida, nunca colada) e **oráculo** para testes diferenciais, rodando como processo externo. | Linkar ou copiar. |
| KallistiOS | licença BSD-like (KOS License) | Documentação de registradores (TA, PVR, AICA, Maple, G1/G2). | — |
| gditools3 / loader Ghidra | **licença não confirmada** | Formato de GDI e mapeamento de LBA, como documentação. | Copiar até confirmar a licença. |

---

## 2. Visão geral

```
 disco do usuário (CDI/GDI/CHD)
        │  tools/extractor  (M1)
        ▼
 game-data/  (IP.BIN, 1ST_READ.BIN, BSDATA/…, manifest.json com hashes)   ← não versionado
        │  translator/analyze  (M3)  + config/bluestinger_us.toml
        ▼
 analysis/*.toml  (funções, tabelas de salto, pools de constantes, overrides)
        │  translator/codegen  (M3)
        ▼
 generated/*.cpp  (uma função C++ por função SH-4, dispatch table)          ← não versionado
        │  CMake + MSVC/Clang
        ▼
 BlueStinger.exe  = generated + runtime/ (+ SDL3, Vulkan)
```

Três binários: `bs-extract` (extrator), `bs-recomp` (analisador + gerador) e
`BlueStinger` (jogo). O fluxo do usuário final é **um launcher** (como no ID AS3)
que roda extrair → recompilar → compilar. Para isso, distribuir o compilador ou
um `.exe` pré-gerado é a decisão D7.

---

## 3. Ingestão (M1)

- **Formatos**: só **CDI** (D3). GDI/CHD entram depois como novas
  implementações de `DiscImage`.
- **CDI** (DiscJuggler v2/v3/v3.5): o trailer tem os últimos 8 bytes = versão +
  offset do cabeçalho (na v3.5 o offset é contado a partir do fim do arquivo).
  O cabeçalho lista as sessões e faixas (modo, pregap, comprimento, LBA inicial,
  tamanho de setor). Os dados das faixas ficam em sequência a partir do offset
  0 do arquivo, cada uma começando após o seu pregap. Na imagem do usuário:
  sessão 1 = LBA 0, 227 955 setores (FMVs); sessão 2 = LBA 239 355,
  120 464 setores (IP.BIN, ISO9660, executável). Setor de 2336 bytes, com
  dados do usuário em +8.
- **Abstração**: `DiscImage` → lista de `Track{lba_inicio, setores, tamanho_setor, offset_dados}`
  e `readSector(lba)` resolvendo a faixa. O ISO9660 é lido sobre isso. Os FMVs
  em outra sessão saem naturalmente.
- **IP.BIN**: parse dos campos e verificação do bit Windows CE (aborta com
  mensagem se for WinCE).
- **Desembaralhamento**: guiado pelo hash. Se o executável bruto não bate com
  nenhuma versão conhecida, o extrator testa a versão desembaralhada
  (algoritmo MIL-CD: fatias de 32 bytes permutadas por PRNG semeado com o
  tamanho). Neste rip o executável não está embaralhado, então esse caminho só
  foi testado por ida e volta em testes unitários, sem dado real.
- **Hash**: SHA-256 (e CRC32 para cruzar com o Redump) de `1ST_READ.BIN` e de
  cada arquivo de dados, gravado em `game-data/manifest.json`. A versão
  suportada fica identificada pelo **hash do `1ST_READ.BIN`**, não da imagem
  inteira, porque rips CDI diferentes divergem (vídeo recodificado, padding).
  O hash dos arquivos de dados é informativo: um aviso em vez de um erro, para
  aceitar rips com FMV recodificado.

---

## 4. Análise (M3)

Endereço de carga e entry point: `0x8C010000` (P1, cacheável).

1. **Decodificador** (M2): uma tabela de 65 536 entradas indexada pelo opcode
   de 16 bits, gerada a partir de uma lista declarativa de padrões
   (`0110nnnnmmmm0011 MOV Rm,Rn`, …) em `translator/decoder/sh4_isa.inc`. Ela
   serve ao analisador, ao gerador e aos testes. São 209 formas, todas de 16
   bits, independentes de FPSCR (PR/SZ mudam a semântica, não a codificação).
   Validação: todos os 65 536 opcodes são comparados com o Capstone 6 por
   `tools/dev/compare_capstone.py`, com cada divergência revisada em
   `tools/dev/capstone_review.txt`.
2. **Descoberta recursiva** a partir do entry point e de ponteiros conhecidos
   (vetores de VBR, callbacks registrados em syscalls):
   - `bsr`/`jsr` criam funções. `bra`/`bt`/`bf` criam blocos.
   - Pares `mov.l @(disp,PC),Rn` + `jsr @Rn` resolvem o alvo pela constant pool.
     **É o padrão dominante no código do SHC (compilador Hitachi/Katana).**
   - Delay slots: a instrução no slot é **emitida antes** do desvio, com o alvo
     e a condição calculados antes (semântica exata do SH-4: o slot vê o T
     original e `rts` usa o PR anterior). Instruções ilegais em slot ficam
     registradas.
   - Constant pools: o fim de uma função é decidido pelo fluxo, nunca de forma
     linear. As palavras referenciadas por `mov.x @(disp,PC)` são marcadas como
     dados.
   - Tabelas de salto: o padrão do SHC é `mova`/`mov.w @(R0,Rn)` + `braf`. O
     reconhecimento é por padrão, com fallback para entrada manual no TOML.
   - Varredura complementar por prólogos (`sts.l pr,@-r15`, `mov.l r14,@-r15`)
     para funções só alcançadas por ponteiro (vtables, callbacks Ninja).
3. **Assinaturas de biblioteca**: Katana, Ninja, Kamui2, Shinobi e Sofdec são
   bibliotecas estáticas da Sega, idênticas em vários jogos. Identificar as
   funções por hash de bytes mascarados (estilo FLIRT) dá nomes e pontos de
   HLE/override. Isso é opcional e incremental.
4. **Ponteiros de função** (o que dá a maior parte da cobertura): palavras em
   dados e constantes do pool que apontam para uma **fronteira de função**
   (logo depois de `rts`/`bra`/`jmp` + slot, de dado do pool, de `nop` de
   alinhamento ou de palavra que não decodifica) **e** têm cara de função:
   prólogo típico do SHC (`sts.l pr`/`mov.l rX,@-r15`/`fmov.s frX,@-r15`/
   `add #-n,r15`), folha curta terminada em `rts`, ou stub/thunk (`bra` para
   código plausível, `mov.l @(d,PC),rX; jmp @rX`). Nenhuma instrução
   privilegiada é aceita no caminho. Iterado até o ponto fixo.
   **Invariantes verificados a cada execução**: zero "fins mortos" (fluxo que
   cai em dado) e zero instruções órfãs (alcançadas na fase 1 sem função na
   fase 2).
5. **Config TOML** (`config/bluestinger_us.toml`): funções extras, fronteiras
   forçadas, tabelas de salto manuais, faixas de dados, instruções a ignorar,
   funções a substituir (override), hooks no meio da função e overlays.

---

## 5. Tradução (M3)

- **Granularidade**: uma função C++ por função SH-4, `void f_8C0123A0(Ctx* c)`.
  Desvios internos viram `goto L_8C0123B4;`. Chamadas diretas viram chamadas C++.
  `rts` vira `return`.
- **Contexto** (`runtime/cpu/sh4_context.h`): `r[16]`, `rbank[8]`, `sr` (com T
  separado em `uint32_t t` para velocidade e montado sob demanda), `gbr`, `vbr`,
  `ssr`, `spc`, `sgr`, `dbr`, `pr`, `mach/macl`, `fpscr`, `fpul`, `fr[2][16]`
  (bancos FR/XF), `pc` (só para diagnóstico) e o contador de ciclos.
- **Memória**: acessos genéricos via `mem_read32(c, addr)`. O caminho rápido
  para RAM é inline (`(addr & 0x1C000000) == 0x0C000000`, que cobre os espelhos
  P0/P1/P2/P3) e todo o resto (MMIO, VRAM, AICA RAM) cai no despacho por região.
  **Opção futura**: reservar 512 MB de espaço virtual com espelhos mapeados
  pelo SO (`MapViewOfFile3`) para eliminar o teste.
- **Chamadas indiretas** (`jsr @Rn`, `jmp @Rn`, `bsrf`, `braf` fora de tabela):
  `sh4_dispatch(c, alvo)` consulta uma tabela gerada (hash perfeito ou array
  esparso por página de 4 KB). Um endereço desconhecido é **registrado em log
  com o PC de origem e o estado**, e a execução para em modo debug ou chama um
  handler configurável. Nunca segue em silêncio.
- **Modos de FPU**: `FPSCR.PR` (single/double) e `FPSCR.SZ` (fmov 32/64) mudam
  a semântica. O analisador propaga o modo conhecido por bloco (o código Katana
  quase sempre roda com PR=0 e alterna SZ em torno de cópias). Onde o modo é
  ambíguo, o gerador emite as duas variantes com um teste em runtime. `FR`
  (troca de banco) usa um índice de banco, sem copiar 32 floats.
- **Arredondamento**: o Dreamcast roda com `FPSCR.RM = 01` (para zero). O
  runtime aplica o modo do guest ao host (`fesetround`) na thread da CPU.
  Consequência: **nenhum código da thread da CPU pode depender da libm** (o
  `sin` da CRT erra o último bit em modo "para zero"). `FSCA` usa uma tabela
  gerada uma vez com arredondamento ao mais próximo. Outras threads (render,
  áudio) não são afetadas, porque o modo é por thread.
- **Precisão**: `FIPR`, `FTRV`, `FSCA` e `FSRRA` têm resultados aproximados no
  hardware. Implementação inicial com float IEEE e `FSCA` por tabela de
  seno/cosseno de 64K entradas gerada em build. Os testes diferenciais medem a
  divergência; se importar para a lógica (colisão, RNG), reproduzimos bit a
  bit. O host liga FTZ/DAZ quando `FPSCR.DN=1` (padrão do Dreamcast). Os campos Cause/Flag/Enable do FPSCR ainda não são atualizados (exige exceções de FPU).
- **Interrupções e tempo** (ponto crítico, ver R1): o código gerado incrementa
  um contador de ciclos estimado por bloco e chama `sh4_poll(c)` em **back-edges
  de laços, chamadas e retornos**. `sh4_poll` entrega VBlank, fim de DMA, fim de
  render do TA e timers TMU **como chamada aninhada** ao handler do VBR (salva
  SSR/SPC, troca de banco, executa o handler recompilado, `rte` retorna). Isso
  substitui o "interromper em qualquer instrução", que é impossível em código
  estático e desnecessário para código Katana (que não depende de interrupção
  no meio de um bloco).
- **Overrides**: funções em `runtime/game/overrides/*.cpp` com o mesmo símbolo
  (`f_8C0xxxxx`) têm prioridade, como no N64Recomp, porque o código gerado vai
  numa biblioteca estática. Uso: substituir rotinas de espera ativa, cópias de
  memória e HLE.
- **Self-modifying code / código em RAM**: o SH-4 exige `ocbwb` + `icbi` para
  código novo. Interceptar escritas em páginas de código e um `icbi`/cache flush
  em região conhecida gera um log de "código dinâmico detectado" (R3).

---

## 6. Runtime (M4–M7)

```
runtime/
  core/      laço principal, scheduler de eventos por ciclo, log, config, savestate (debug)
  cpu/       contexto SH-4, helpers (div0/div1, mac, fpu), poll de interrupção, INTC
  memory/    mapa de 29 bits, RAM 16 MB, espelhos, store queues (sq0/sq1 → TA/RAM), DMAC (ch2 → TA)
  hle/       syscalls de BIOS (vetores em 0x8C0000B0..E0: SYSINFO, ROMFONT, FLASHROM, GDROM, MISC),
             estado de boot pós-BIOS (registradores, IP.BIN em 0x8C008000)
  gdrom/     GD-ROM via HLE das syscalls GDROM, lendo game-data/ (arquivo por LBA via manifest)
  pvr/       registradores PVR/TA, parser de listas do TA, conversão de texturas (twiddled, VQ,
             paletas, ARGB1555/4444/565/YUV422), renderer Vulkan
  aica/      registradores AICA, 64 canais, DSP, ARM7 (ver D4), saída via SDL3 audio
  maple/     controle (SDL3 gamepad/teclado), VMU como arquivo .bin em saves/
  platform/  janela SDL3, Vulkan instance/swapchain, input, caminhos
  game/      específico do Blue Stinger: overrides, hooks, melhorias (M9)
```

- **PowerVR2**: emulação LLE no nível de registradores/listas do TA (genérico,
  independente de Kamui). O renderer Vulkan monta a cena por frame
  (opaque/punch-through/translucent, com ordenação ou OIT para translucentes
  depois). Resolução interna escalável desde o início (M9 fica barato).
- **Vídeo Sofdec**: o jogo decodifica o MPEG-1 **na CPU SH-4**, então o
  recompilado já decodifica sozinho. Fica como opção de override para
  decodificador nativo se o desempenho exigir.
- **Threads**: a CPU guest roda na thread principal. Áudio numa thread SDL
  alimentada por um ring buffer. Renderização na thread principal no início.

---

## 7. Validação

- **Unitários (M2)**: decodificador exaustivo (todas as 65 536 palavras
  classificadas; disassembly comparada a uma lista de referência escrita à mão).
  Semântica por instrução executada sobre o `Ctx`, incluindo casos de borda
  (`div1`, `mac.w` com saturação, `shad` negativo, flags T, `ftrc` com overflow).
- **Diferenciais**: ferramenta `tests/diff/` que executa uma função recompilada
  a partir de um estado inicial (registradores + RAM) e compara o resultado com
  o mesmo estado executado no **Flycast como processo externo** (via GDB stub
  do Flycast). Sem linkagem com o Flycast, então a GPL dele não contamina.
- **Rastros**: em modo debug, o runtime grava traços de chamadas
  (`f_ADDR entrada/saída` + hash de registradores) comparáveis a um trace do
  Flycast.
- **STATUS.md**: cada item verificado registra como foi verificado (teste,
  diff, observação visual) e com qual versão.

---

## 8. Decisões

Registradas em 2026-10-13.

| # | Decisão | Escolha |
|---|---|---|
| D1 | Licença do projeto | **MIT** (arquivo `LICENSE`). Implementação do zero, sem código GPL. |
| D2 | Versão-alvo | **NTSC-U V1.000 (T13001N)**, só essa por enquanto. |
| D3 | Formatos de entrada | **Só CDI por enquanto.** A abstração `DiscImage` deixa GDI/CHD para depois sem retrabalho. |
| D4 | ARM7 do AICA | **Interpretador ARM7** no M6. A regra "sem interpretador" vale para o SH-4. Recompilar o `MANATEE.DRV` fica como otimização futura. |
| D5 | Linguagem | **C++20 para tudo** (extrator, tradutor, runtime). Sem Python para o usuário final. |
| D6 | Compilador | Clang-cl como alvo principal e MSVC sempre compilando. Enquanto o clang-cl não estiver instalado, o MSVC é usado. |
| D7 | Distribuição | Em aberto até o M8. Até lá, o usuário compila. |

## 9. Riscos

| # | Risco | Impacto | Mitigação |
|---|---|---|---|
| R1 | Interrupções e temporização em código estático (VBlank, DMA, fim de render, laços de espera ativa em registradores) | Travamento no boot ou timing errado | Poll em back-edges + contador de ciclos. Overrides para laços de espera conhecidos. Scheduler de eventos por ciclo. Trace comparado ao Flycast. |
| R2 | Ponteiros de função e vtables (Ninja usa callbacks; o jogo pode usar tabelas de estados) | Funções não descobertas | Varredura de prólogos + dispatch com log de misses + TOML. Cada miss vira entrada no config e o tradutor roda de novo. |
| R3 | Código fora do `1ST_READ.BIN` | Exigiria recompilar overlays | **Reduzido no M3**: os `R*.BIN` são dados (nenhum epílogo de função em 265 arquivos). Resta código copiado/gerado em RAM em tempo de execução: o despacho registra todo desvio para endereço sem função. |
| R4 | Modos de FPU ambíguos (PR/SZ/FR) | Resultados float errados em silêncio | Propagação de modo + variantes com teste em runtime + asserts em debug. |
| R5 | Aproximações de FSCA/FSRRA/FIPR | Divergência sutil de física ou IA | Testes diferenciais. Tabela de FSCA idêntica à do hardware se necessário. |
| R6 | Store queues + DMA para o TA | Geometria faltando | Implementar SQ → TA cedo (M4). É o caminho principal do Kamui. |
| R7 | Rip CDI ≠ GD original (FMV possivelmente recodificado, layout diferente) | Hash não bate com o Redump | Identificar a versão pelo `1ST_READ.BIN`. Suportar GDI/CHD também. |
| R8 | Tamanho do código gerado (~1 MB de código SH-4 → centenas de milhares de linhas de C++) | Build lento | Dividir a saída em N arquivos, Clang, build com unity/PCH. Só regerar o que mudou. |
| R9 | Licenças de referências sem licença declarada (ID AS3, gditools3) | Contaminação legal | Ler só a documentação. Não copiar código. Registrar em LEGAL.md. |
| R10 | Escrita em VRAM / framebuffer direto pela CPU (telas de loading, FMV) | Imagem ausente | Caminho de framebuffer direto no renderer (FB_R_SOF1 → textura) desde o M4. FMV Sofdec depende disso. |

---

## 10. Marcos (resumo)

M0 esqueleto · M1 extrator + hash · M2 decodificador + testes · M3 descoberta +
C++ que compila · M4 primeiro frame · M5 PVR2 completo · M6 áudio · M7
controle/VMU · M8 zerável · M9 melhorias. O acompanhamento fica em `STATUS.md`.
