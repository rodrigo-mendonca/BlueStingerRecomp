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
  Implementado no M4 (`cpu/sh4_intc.cpp`): `BS_POLL(retomada)` nos back-edges e no
  retorno de cada chamada; checagem imediata (`BS_IRQ_CHECK`) depois de `ldc …,sr`
  e de `pref` (um envio ao TA pode gerar interrupção na hora; o jogo depende de o
  tratador rodar antes da fila de DMA seguinte). Depois do `rte` do tratador, a
  checagem se repete antes de retomar.
- **`rte` para outro contexto e threads do guest** (M4, `cpu/sh4_threads.cpp`): o
  Blue Stinger tem um escalonador próprio que troca de thread com `rte` (SPC/SR e
  R15 de outro contexto). Cada chamada guarda `ret_expect` (PC de retorno) e
  `ret_sp` (R15). Num `rte` cujo alvo difere do esperado: se alvo e R15 casam com uma
  thread suspensa, retoma aquela; se R15 é o esperado, é continuação na mesma
  thread (rotina adiada do tratador), despachada na pilha atual; senão, é uma thread
  nova. Cada thread do guest roda numa fibra do Windows (pilha C própria); o
  núcleo da CPU só conhece `sh4_rte_redirect`, então a troca de fibras por outro
  mecanismo (threads do host com passagem de bastão, ucontext) não muda o código
  gerado. O modo de FP do host é reaplicado a partir do FPSCR a cada troca.
- **setjmp/longjmp entre threads** (M4, decisão do mantenedor de 2026-10-14: suporte
  genérico, não HLE da biblioteca): a biblioteca de threads cooperativa do jogo troca
  de contexto com setjmp/longjmp, declarados em `[threads]` no TOML. O gerador marca o
  retorno de cada chamada a setjmp como **ponto de reentrada** (rótulo na própria
  função). Nessas funções as chamadas usam `BS_CALL_REENTRANT`, e no fim há um
  despacho `bs_reenter_` para os rótulos. O `rts` de uma função longjmp usa
  `BS_LONGJMP_RET`. Com alvo inesperado, `sh4_longjmp_redirect` retoma a thread
  suspensa cuja chamada pendente tem o R15 restaurado e marca `c->redirect` com o
  alvo. Senão, o alvo começa uma thread nova (contexto montado pelo jogo, com PR =
  entrada e pilha nova). A fibra retomada sobe pelos quadros (todo `BS_CALL` retorna
  com `redirect` pendente) até o quadro cujo R15 na chamada é o restaurado, e salta
  para o rótulo. Assim o código real do jogo continua a partir do retorno do setjmp,
  com os registradores que o longjmp do guest restaurou. Fora desses casos (longjmp
  para quadro da própria thread, alvo desconhecido, ponto que não é reentrada) o
  resultado é erro fatal, nunca silêncio. O mecanismo não depende de fibras: só de
  "retomar a thread X" e de `c->redirect`.
- **Código copiado para a RAM** (R3): segmentos `[[relocated]]` na configuração
  (destino, origem, tamanho); as instruções são analisadas no destino, o pool é
  lido da RAM e o runtime confere as instruções antes de executar
  (`sh4_check_code`). Usado para os vetores de exceção do Katana em VBR. O pool
  nunca é embutido (`inline_pool_constants = false`): o jogo grava em palavras do
  pool.
- **Syscalls do sistema** (M4; N8.3, `docs/N8-3-BOOT.md`): os vetores de syscall em
  `0x8C0000B0+` apontam para `0x8C001000+`, onde há funções nativas registradas no
  despacho (`sh4_register_hle`):
  - no produto, o serviço nativo `system/syscall_service`, só com o contrato do jogo (GD-ROM: INIT, versão e leitura
    de setores dos dados; flash: partição 2 volátil; sysinfo: ID);
  - fora do contrato, erro explícito;
  - o modelo HLE (M4, `hle/`, `gdrom/`: BIOS, drive com `DiscSource` e flash) fica só na validação e nas ferramentas
    (o CDI do usuário por `gdrom/image_source.cpp`).
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
  system/    N8.3: serviço nativo das syscalls (contrato do jogo) e a área do sistema (vetores, sysinfo)
  hle/       modelo HLE das syscalls de BIOS (SYSINFO, ROMFONT, FLASHROM, GDROM, MISC): validação e ferramentas
  gdrom/     modelo do drive de GD-ROM (fonte de setores, TOC, tempo reios): validação e ferramentas
  boot/      boot nativo: estado de entrada medido (área do sistema, IP.BIN em 0x8C008000, registradores)
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
  (Revisto no M4: o executável roda o jogo numa thread própria, ver abaixo.)

### Executável nativo (M4, 2026-10-14)

```
app/
  game_host.{h,cpp}  núcleo sem janela (bs_app_core): thread do jogo, boot, ritmo, quadros
  main_sdl.cpp       bluestinger.exe: janela, apresentação e eventos pelo SDL3
runtime/
  boot/              boot nativo a partir do CDI (sem BIOS, sem snapshot)
  pvr/pvr_video.*    quadro exibido pelo PVR (FB_R_*) e gancho de VBlank
  input/             fontes de entrada (eventos de teclado, XInput, roteiro)
```

- **SDL3 isolado**: entra por FetchContent (3.2.30, estático, licença zlib) e só no
  alvo `bluestinger` (`BS_BUILD_GAME=ON` + `generated/`). O runtime, os testes e as
  ferramentas não dependem dele. Trocar de camada de janela é trocar `main_sdl.cpp`.
- **Duas frequências**: o jogo roda na thread do `GameHost`. A cada VBlank-in emulado,
  o gancho `pvr_set_vblank_hook` publica o quadro exibido (troca de buffer sob mutex) e
  segura o ritmo em 59,94 campos por segundo de relógio. Se atrasar mais de 250 ms,
  recomeça a contagem em vez de acelerar. A janela apresenta na frequência do monitor
  (vsync) o quadro mais recente. Com isso, o tempo do Dreamcast nunca depende da
  apresentação.
- **Quadro exibido, não o renderizado**: `pvr_video_frame` lê o que o hardware mostra
  (FB_R_CTRL/SIZE/SOF1/SOF2, campos entrelaçados intercalados, blank/vídeo desligado =
  preto). A janela usa área lógica 640x480 com letterbox (4:3 ao redimensionar).
- **Entrada**: a janela entrega eventos de tecla por nome ao `KeyEventInput`
  (runtime/input; nada é lido globalmente; perder o foco solta as teclas). O XInput é
  combinado a ele. O Maple lê a fonte combinada. Roteiros (`BS_INPUT_SCRIPT`) só sem
  janela.
- **Encerramento**: fechar a janela pede a parada; o gancho de VBlank lança
  `StopRequest`, que atravessa o código gerado e as fibras (uma fibra que não é a thread
  0 devolve a exceção à thread 0, ver `sh4_threads`). Na thread do jogo, o host apaga as
  fibras (`sh4_threads_shutdown`), solta o gancho e fecha a imagem de disco. O estado das
  threads do guest é `thread_local`: pertence à thread do host que roda o jogo.
- **Boot nativo**: `boot_read_disc` lê IP.BIN e 1ST_READ.BIN do CDI e exige o hash da
  versão recompilada. `boot_prepare` monta o estado de entrada medido no oráculo
  (`bios_entry_state.inc`: registradores do PVR e variáveis do sistema, nenhum byte do
  jogo). Os restos da rotina de boot do IP.BIN em 0x8CDE0000+ não são reproduzidos (ver
  STATUS.md).
- **Poll central** (`sh4_service`): avança a leitura do GD-ROM em curso, sincroniza os
  eventos do Holly/SPG (VBlank anda mesmo com SR.BL, porque o vídeo não depende da
  máscara da CPU) e só então tenta entregar interrupções.
  - **Tempo estável e legado:** roda a cada quantum de ciclos estimados (`kServiceQuantum`).
  - **Tempo lógico (N8.1, cadência B):** roda só quando pedido. Os pedidos vêm da espera que andou o instante, do
    fim do DMA, de um nível do Holly que ficou entregável (`sh4_irq_wakeup`) e de evento agendado no instante atual.
    A guarda de vivacidade vai por um tick a cada `kProgressTick` polls (`next_poll`), sem ciclos.
  - A espera do hardware restaura o ponto de retomada antes de cada atendimento, para o SPC ser confiável.
  - Detalhes em `docs/N8-1-PREPARACAO.md` seção 7.
- **Rasterizador e o host**: o render roda num ambiente de ponto flutuante fixo (MXCSR
  padrão), isolado do FPSCR do SH-4 que o runtime aplica à thread do jogo. A resolução de
  autosort/punch-through agrupa fragmentos por pixel e resolve as linhas em paralelo
  (`std::execution::par`), com o mesmo resultado da ordem sequencial. Há também um cache
  de texels por render.
- **Dados do jogo**: o executável não embute nada do disco; lê o CDI do usuário ao
  iniciar. Mas ele **contém o código gerado** a partir do 1ST_READ.BIN do usuário. Por
  isso o binário compilado é derivado da cópia do usuário e não deve ser redistribuído.
  A forma de distribuição (D7) continua para o M8.

### Independência do CDI: dados de execução (2026-10-14)

Exigência do mantenedor: o CDI é **entrada do Builder**, nunca dependência do jogo. O
executável não aceita mais imagem de disco; lê a pasta `data\` ao lado dele.

```
<instalação>\data\
  disc.manifest   texto: formato, versão, faixas, arquivos (LBA, tamanho, SHA-256, caminho)
  disc\<caminho>  os 2 092 arquivos do ISO9660, com os bytes exatamente como no disco
  disc_meta.idx   LBAs dos setores que não vêm de arquivo (71 no NTSC-U): área do sistema
  disc_meta.bin   (IP.BIN), descritores, diretórios, sobras não nulas do último setor
```

- **Camada própria** (`tools/extractor/runtime_data`): o serviço nativo das syscalls (N8.3) e, na validação, o modelo
  do GD-ROM (`runtime/gdrom/data_source`) pedem setores ao `RuntimeDisc`. Ele reconstrói
  cada setor de dados: setor de sistema, senão o trecho do arquivo que o cobre (zeros
  depois do fim), senão zeros. Assim o comportamento do jogo é preservado sem a
  estrutura física do disco: não há faixas brutas, subcódigo nem setores de 2 336 bytes.
- **Prova de equivalência**: na preparação, o Builder compara **todos** os setores das
  faixas de dados (~360 mil) com a imagem (`verify_runtime_data`). O teste `runtime_data`
  repete isso com o CDI real e confere que o boot a partir de `data\` é igual ao boot a
  partir do CDI.
- **Boot**: `boot_read_data` lê IP.BIN (meta) e 1ST_READ.BIN (arquivo) e exige o hash da
  versão recompilada. Dados ausentes, incompletos ou de outro formato geram mensagem com
  o arquivo que falta e o que fazer (gerar de novo com o Builder).
- **No Builder**: preparado uma vez por saída em `.bs-build\data` (marca + SHA-256 de
  cada arquivo para reaproveitar). Entra no pacote por **hard links** (sem copiar 708 MB;
  cópia se o volume não aceitar). Arquivos de dados nunca são reescritos no lugar: uma
  nova preparação cria pasta nova, então os links não misturam versões. A verificação do
  executável montado roda `--verify-data` e o boot de teste **sem argumento de CDI**.

### Analisador: tabelas de ponteiros (NEW GAME, 2026-10-14)

O NEW GAME parava em `PC=8C1953E2`, um `jsr @r3` com `r3` lido de uma tabela de métodos
de objeto. O destino `8C194580` (`rts; mov #0,r0`) só aparece no binário como dado, na
vtable em `8C1DBF58`. Duas regras anti-falso-positivo o recusavam: folha de 2
instruções e "`rts` sozinho só com `nop` no slot". A correção é geral: **sequências de
palavras em dado que apontam para dentro da imagem** (pelo menos 3 entradas, pelo menos 2
já funções conhecidas) são tabelas de ponteiros, e suas outras entradas viram funções
com evidência mais fraca no corpo. Continuam exigidos a fronteira de função, o corpo de
código e o desvio incondicional com slot válido. Resultado: 45 funções novas (44 stubs de
2 instruções), 0 fins mortos, 0 órfãs. O valor do desvio muda entre execuções
(`8C194B80` no relato, `8C194580` aqui) porque vem de memória.

### PowerVR2: conversor YUV, VQ e mipmap (2026-10-14)

- **Conversor YUV do TA** (`runtime/pvr/ta.cpp`): macroblocos YUV420 (384 bytes: U 8x8,
  V 8x8, Y 4×8x8) ou YUV422 (512) enviados a `0x10800000` viram textura UYVY na VRAM em
  `TA_YUV_TEX_BASE` (uma textura de (U+1)·16 × (V+1)·16 ou uma 16x16 por macrobloco).
  `TA_YUV_TEX_CNT` conta, e o fim do quadro acende ISTNRM bit 6. Escrever BASE/CTRL
  recomeça a conversão.
- **Texturas**: YUV422 linear (coeficientes do PowerVR2), **VQ** (codebook 256 × 2x2 + um
  índice por bloco, twiddled) e **mipmap** (amostra o maior nível; deslocamentos 16 bits
  `6 + 2T`, paleta 8 bpp `3 + T`, VQ `1 + ((W/2)² - 1)/3`). Faltam YUV twiddled, bump e
  paleta 4 bpp com mipmap (registrados por TCW e omitidos).

### Mods e projeto C++ editável (2026-10-14)

Objetivo de longo prazo do mantenedor: controle do código para mods. O código gerado é a
tradução fiel das instruções SH-4 (não um código-fonte "como o original"). Por isso os
pontos de extensão ficam na fronteira das funções e dos arquivos, sem exigir edição das
milhares de funções geradas.

- **Funções** (`runtime/mods/mod_api.h`): `BS_MOD(nome) { bs::mods::replace(endereço,
  função); }`, `call_original`, `reset`. O codegen emite toda chamada entre funções
  geradas (diretas, tail calls, tabelas de salto locais) pela macro `BSG_FN(addr)`. No
  build normal ela é a chamada direta (sem custo). Com `BS_MODS=ON`, é a chamada pelo
  slot da função (`slot_XXXXXXXX`, ponteiro global), e `replace` troca o slot e registra a
  substituição para os desvios indiretos (`sh4_override`, consultado antes da tabela
  gerada).
- **Arquivos** (`<instalação>\mods\assets\<caminho do disco>`): o `RuntimeDisc` lê o mod
  no lugar do arquivo original (inclusive no último setor, que no disco vem da meta).
  Precisa caber no espaço original: maior é recusado com mensagem. Realocar arquivos
  maiores (novo LBA + registro de diretório) fica para depois.
- **Projeto editável** (`builder/project_export`, `bs-builder --projeto`):
  - O que contém: fontes do runtime/app/leitor, SDL3, C++ gerado, `mapa_funcoes.tsv`
    (endereço → `f_XXXXXXXX` → arquivo → origem → tamanho), `mods/` com exemplo,
    `LEIA-ME-PROJETO.md` (estrutura, API, limitações) e `compilar.cmd`.
  - Fica numa pasta privada do dono do CDI, nunca no repositório nem na distribuição do
    Builder.
  - Reexportar não toca em `mods/`, não sobrescreve edições mais novas em `runtime/`/`app/`
    e guarda o `generated/` anterior em `generated.anterior-<data>`.
  - `bs-builder --de-projeto` compila o projeto com os mods pelo mesmo fluxo verificado
    (sem CDI se os dados já estão preparados) e copia `mods/assets` para a instalação.

### Plano de áudio nativo: AICA, ARM7 e Manatee (SUBSTITUÍDO em 2026-10-15)

> **Substituído** pela diretriz do port nativo (2026-10-15): o executável final não pode
> executar nem interpretar o ARM7, nem simular a AICA. O plano vigente é
> `docs/PLANO-PORT-NATIVO.md` (etapa N6: motor de som nativo que lê o `BS_SND.MLT`; o
> `MANATEE.DRV` só é analisado estaticamente). O texto abaixo fica como registro da
> medição e das alternativas descartadas.

**Situação medida** (2026-10-14, `BS_AICA_TRACE`):
- O jogo lê do disco `BSDATA/MANATEE.DRV` (33 KB, driver de som do ARM7) e `BS_SND.MLT`
  (1,7 MB, banco de sons) e os copia **pela CPU** para a RAM de som (457 753 palavras
  escritas). O DMA do G2 (`SB_AD*`) só é configurado, nunca disparado.
- Depois, o SH-4 consulta a área de trabalho do driver (0x00813400, 0x00813A00/18...) a
  cada campo, por uma rotina genérica de leitura da RAM de som (retorno em `8C08EC4A`).
  Sem o ARM7, essa área nunca muda (fica com o preenchimento "SEGA" que o próprio jogo
  grava).
- O jogo segue sem som, e o vídeo NEWGAME.SFD termina cedo. A ligação entre as duas coisas
  ainda não está provada (STATUS.md).

**Peças** (todas novas em `runtime/aica/`; nada de GPL, Flycast só como referência de
comportamento e como oráculo):
1. **ARM7DI** (ARMv4, só modo ARM; D4 já decidiu interpretador): registradores com bancos
   por modo, CPSR/SPSR, processamento de dados com shifter e flags, LDR/STR/LDM/STM,
   MUL/MLA, desvios, MRS/MSR, SWI, IRQ/FIQ. Mapa: 0x000000-0x1FFFFF RAM de som,
   0x800000+ registradores do AICA. Recompilar estaticamente o MANATEE.DRV fica como
   otimização depois (o mesmo fluxo do SH-4, num binário de 33 KB).
2. **Registradores do AICA**:
   - 64 canais × 0x80 bytes;
   - registradores comuns (0x2800+): volume mestre, monitor MSLC/CA (posição do canal),
     timers A/B/C, interrupções SCIEB/SCIPD/SCIRE (para o ARM) e MCIEB/MCIPD/MCIRE (para
     o SH-4), reset do ARM em 0x2C00 (hoje só guardado);
   - RTC (já existe).
3. **Síntese**: PCM 16/8 bits e ADPCM Yamaha, laço, envelope AEG
   (attack/decay1/decay2/release), FEG/filtro, LFO de pitch/amplitude, volume/pan, mistura
   a 44,1 kHz.
4. **DSP** (efeitos, microprograma de 128 passos): segunda fase. O som sai seco antes dela.
5. **Interrupções**: AICA → SH-4 pelo Holly (ISTEXT), SH-4 → ARM pelo MCIPD.
6. **Tempo**: o ARM e a síntese andam no poll central (`sh4_service`) em fatias de ciclos
   do SH-4. Assim a execução é determinística (testes, roteiros) e independente do host.
7. **Saída**: o runtime produz amostras num buffer circular. O `app/` entrega ao SDL3
   (stream de áudio). O headless pode gravar WAV (testes).
8. **G2 DMA** (`SB_ADST`...): o Blue Stinger não usa nesta fase, mas fica simples
   implementar junto.

**Reaproveitado do runtime atual**:
- scheduler/poll central e sincronização de eventos (`holly_intc_sync`);
- controlador de interrupções do Holly;
- despacho de memória (`aica_reg_read/write`) e RAM de som;
- GameHost (ritmo, thread) e a camada SDL3 do `app/`;
- framework de testes, `bs-diff` e o oráculo. O stub GDB do Flycast lê a RAM de som (lido
  `EAFFFFFE` em 0x00800000), então dá para comparar a área do driver.

**Testes necessários**:
- ARM7: um teste por classe de instrução (flags, shifts com carry, modos de endereçamento,
  LDM/STM com writeback e base na lista, trocas de modo e exceções), com valores escritos à
  mão a partir do manual ARM.
- AICA: decodificação ADPCM com vetores conhecidos, laço e pontos de fim, tabela de tempos
  do AEG, mistura/pan; hash de WAV curto gerado headless (regressão).
- Integração: o driver sobe (área de trabalho deixa de ser "SEGA" e passa a ter o formato
  do oráculo num ponto de parada alinhado). O alinhamento ainda não existe: a contagem de
  passagens pela rotina de leitura difere entre os dois, ver STATUS.md.
- Ponta a ponta: música do título e efeitos no WAV (não silêncio, sem estouro); vídeo
  NEWGAME.SFD até o fim, se a dependência for confirmada.

**Sequência sugerida** (M6):
- 6a: registradores + ARM7 + driver subindo, comparado ao oráculo;
- 6b: canais, mistura e saída SDL3;
- 6c: DSP;
- 6d: desempenho (recompilação do driver ARM, se o interpretador pesar).

### Distribuição Preview (2026-10-14)

`tools/dist/make_preview.ps1` monta `dist/BlueStinger-Preview/` (uso local, não
publicável): exe Release, runtime do MSVC app-local, instruções, versão e licenças. A
montagem é feita numa pasta temporária, confere dependências (`dumpbin`) e ausência de
caminhos de desenvolvimento nos binários, e só então troca a Preview anterior (com
restauração se a troca falhar). Dependências de build: Visual Studio Build Tools 2026
(v18, MSVC 14.50, runtime VC145, com o CMake e o Ninja que ele traz; CMake ≥ 3.25), Git
(versão e FetchContent do SDL3/toml++), rede no primeiro configure. O passo seguinte é o Builder (D7), que faz a recompilação na máquina do
usuário a partir do CDI.

### Builder (D7, 2026-10-14)

Objetivo: o usuário sai do próprio CDI para um `BlueStinger.exe` nativo, com recompilação
real na máquina dele. Nenhum binário pronto, nada do jogo na distribuição, sem Flycast.

```
builder/
  builder_core.*    fluxo completo (biblioteca, sem janela)
  toolchain.*       vswhere -> vcvars64 (ambiente em UTF-16) -> CMake/Ninja/CRT do VS
  child_process.*   processos filhos: pipe, UTF-8/OEM/UTF-16, job object (cancela a árvore)
  main_cli.cpp      bs-builder.exe (automação, testes)
  main_recompiler.cpp  BlueStinger-Recompiler.exe (janela simples: idioma, modo portatil)
  main_developer.cpp   BlueStinger-Developer.exe (projeto C++ editavel, log tecnico)
  ui/                  textos da janela simples por chave (pt-BR, en-US)
translator/app/recomp.*   análise + geração como biblioteca (bs-recomp e Builder)
```

- **Escolhas do mantenedor** (2026-10-14): janela **Win32 nativa** (controles comuns v6,
  diálogos nativos, DPI por monitor; sem dependência nova). Compilação com **kit de
  fontes + CMake/Ninja** do próprio Visual Studio do usuário: o Builder leva as fontes do
  runtime/app/leitor de disco (MIT) e do SDL3 (zlib), e tudo é compilado com o toolset
  do usuário (sem incompatibilidade de ABI de `.lib` pré-compilada).
- **Etapas**: entradas → compilador → disco (hash do 1ST_READ) → análise + C++ (em
  processo) → CMake (`BS_BUILD_TOOLS=OFF`: sem tradutor, testes nem rede; SDL3 por
  `FETCHCONTENT_SOURCE_DIR_SDL3`, versão por `BS_VERSION_OVERRIDE`) → compilação →
  pacote numa pasta temporária → **verificação** (`--version` e boot headless de 120
  VBlanks com o CDI do usuário) → troca. Só depois da troca o resultado é sucesso.
- **Nunca destruir a versão anterior**: a pasta nova entra por rename no mesmo volume; a
  anterior vira backup e só sai depois. Falha na troca restaura (e, se nem isso der
  certo, a mensagem diz onde está a anterior); uma sobra de troca interrompida é
  recuperada na próxima execução. Cada instalação leva `.bs-package` (lista dos arquivos
  do pacote: o resto da pasta é do usuário e vai para a versão nova) e `.bs-install`
  (instante; só backups com essa marca são recuperados ou apagados). Pacotes temporários
  têm a marca `.bs-stage` e criação exclusiva. A trava `.bs-build\.bs-lock` permite uma
  execução por pasta de saída. O cancelamento vale até o primeiro rename da troca.
- **Caminhos**: o Ninja do VS (1.12.1) usa as APIs "A" do Windows (`FindFirstFileExA`),
  e o MSVC não passa de 260 caracteres. Por isso tudo o que o Ninja vê (build do CMake,
  C++ gerado e, se o caminho do kit não couber na página ANSI ou for longo, uma cópia do
  kit) fica num cache curto: `%LOCALAPPDATA%\BlueStingerRecomp\build\<hash da saída>`
  (ou `%ProgramData%`, ou `BS_BUILDER_CACHE`). A pasta de saída e a do Builder podem ter
  qualquer caractere. O cache permite recompilação incremental (~10 s).
- **Dependências do usuário**: Build Tools para Visual Studio com "Desenvolvimento para
  desktop com C++" (MSVC x64, SDK do Windows, CMake/Ninja). Independência do Visual
  Studio (clang-cl + SDK) fica para avaliar depois: o SDK do Windows e os headers do MSVC
  não são redistribuíveis livremente.
- **Distribuição** (`tools/dist/make_builder.ps1` → `dist/BlueStinger-Builder/`): os dois
  executáveis, runtime do MSVC, licenças (MIT, SDL3 zlib, toml++ MIT), LEIA-ME e o kit
  (~28 MB). O kit entra por allowlist (`git ls-files --cached --others
  --exclude-standard`: nada que o `.gitignore` exclui). A montagem também recusa nomes de
  arquivos do jogo e procura, em todos os arquivos, amostras do 1ST_READ.BIN/IP.BIN locais
  e caminhos de desenvolvimento.

---

### Port nativo, N0: fundação (2026-10-15)

Detalhes, números e reprodução em `docs/N0-RELATORIO.md`.

- `tools/dcmap`: mapa de regiões do hardware → subsistema que o substitui.
- `bs-hwscan` (`translator/hwscan`) produz:
  - o manifesto de acessos (estático + sonda);
  - a identificação do SDK por camada e família, com evidências e confiança;
  - a fronteira jogo/SDK declarada (`config/fronteiras_us.toml`);
  - o fechamento, a dominância e as métricas com catraca (`config/metricas_base_us.toml`).
- Runtime legado:
  - `c->cur_fn` (salvo e restaurado por `BS_CALL`, por thread e nas interrupções);
  - sonda `BS_HW_PROBE`;
  - trace de ordem `BS_EVENT_TRACE`.
  - Desligados, não mudam o comportamento.
- `native/`: interfaces C++20 sem nada do Dreamcast (arquivos, tempo/eventos, controles,
  saves, vídeo, áudio, render).
- `transitional/`: handles de 32 bits com geração, adaptador `Sh4Context` ↔ nativo e
  registro de provedores legado/nativo.
- `legacy/`: o runtime atual com nome de alvo.

### Port nativo, N1: sistema de arquivos (2026-10-15)

Detalhes em `docs/N1-PLANO.md` e `docs/N1-RELATORIO.md`.

- A API gdFs do executável é atendida por `transitional/gdfs_adapter` sobre
  `native::files::IFileSystem`, implementado por `native/platform/data_files` (arquivos de
  `data\disc` e `mods\assets`, sem setores).
- As funções da API são chamadas pelo slot em todos os builds (`[native] slots`), e o provedor
  é escolhido em execução: `--arquivos nativo` (padrão) | `legado` (referência e rollback).
- Ainda legados até o N8.3: inicialização do gdFs, estado do drive, boot e GD-ROM HLE. No N8.3 o estado do drive e as
  leituras do boot passaram ao serviço nativo das syscalls; o `--arquivos legado` ficou só na validação.

### Port nativo, N2a: relógio lógico e fila de eventos (2026-10-15)

Detalhes em `docs/N2-PLANO.md` e `docs/N2a-RELATORIO.md`.

**Interfaces nativas:** `native/platform/time.h`.
- Instante em ticks numa base de tempo inteira (`TimeBase`).
- Fila com ordem total: instante, prioridade do tipo, ordem de agendamento.
- `post`/`cancel`/`reschedule` tipados; prioridade por tipo configurável (`set_kind_priority`).
- Cancelar uma inscrição vale na hora, inclusive durante a entrega.
- `ITaskScheduler` só declarada, para o N2b.
- Implementação: `LogicalClock` e `EventQueue`.

**No runtime:** os módulos não medem mais tempo pelo contador de ciclos. Pedem a um provedor
(`runtime/cpu/time_hooks.h`):
- os eventos agendados vencidos: VBlank-In/Out, fim de render, fim de DMA do Maple e bloco do
  GD-ROM;
- o instante atual, para TMU, SPG_STATUS, MDST e RTC.

A entrega à CPU continua nos mesmos pontos: poll, acesso aos registradores e nível IRL.

**Provedores** (`--tempo nativo|legado|experimental`):
- legado: `runtime/cpu/time_legacy.cpp`, o cálculo preguiçoso de antes reunido num só arquivo;
- nativo: `transitional/time_adapter`, com relógio e filas nativos, ainda movidos pelos ciclos de compatibilidade;
- experimental: o mesmo adaptador com tempo lógico movido apenas em pontos de espera e esperas de hardware.

Desde o fechamento funcional do N2 (2026-10-24), o experimental é o padrão **na janela** e `--tempo nativo` é o
recuo. O headless continua no nativo estável para preservar as baterias, referências e gravações antigas até o N2c,
na abertura do N8. Uma opção `--tempo` explícita sempre prevalece.

Durante o N2a o progresso ainda vem dos ciclos estimados (`BS_CYCLES`/`BS_POLL`), que entram
só como instante de compatibilidade: um tick por ciclo. Isso sai no N2b.

**Relógio de parede:**
- jogo normal: hora local do host no boot;
- testes e roteiros: `BS_RELOGIO_FIXO` (D5).

### Port nativo, N2b-0/N2b-1: diagnósticos e modo sombra (2026-10-15)

Detalhes em `docs/N2b-PLANO.md` e `docs/N2b-RELATORIO.md`. O N2b-2 não está autorizado.

**Interface nativa:** `native/platform/time.h` troca a `ITaskScheduler` por `IProgress` (D4):
- só observa trabalho e esperas e diz se o tempo poderia andar;
- nenhum método escolhe ou alterna threads; quem troca continua sendo o despachante do SDK
  recompilado.

Implementação: `native/platform/progress.*` (`ProgressMonitor`).

**Pontos de espera:**
- declarados em `[native] esperas` no config;
- o gerador emite `BS_WAIT(pc, destino)` no back-edge (o poll de sempre continua dentro) e
  `BS_WAIT_END(pc)` na instrução seguinte, por onde passam todas as saídas;
- o jogo não muda: o laço continua testando a condição.

**Trabalho estrutural:** `Sh4Context::polls` conta os pontos de controle executados (`BS_POLL`).
É a métrica de falta de progresso do D8; não é tempo e não usa ciclos.

**Modo sombra:**
- o provedor nativo alimenta o monitor (`progress`, `wait_point`, `wait_end`);
- voltas repetidas numa espera são acumuladas, para custar perto de zero;
- a execução real não muda: o trace de ordem é igual ao legado byte a byte.

**Diagnósticos** (desligados por padrão):

| Variável | O que faz |
|---|---|
| `BS_PROGRESS_TRACE` | episódios de espera, avanços, janelas, trabalho por thread e por VBlank |
| `BS_TMU_DIAG` | `quadro`, `agendado`, `zero`, `dobro` |
| `BS_CRI_DIAG` | contagem e salto de passadas do servidor do CRI (`contar`, `pular*`, `menos1`, `repetir3`); classificação trabalho × ociosa e bloqueio simulado (`fluxo`, `pular5`, `repetir5`); consumidor artificial do TMU (`consumidor`). Só no build com mods, nunca no produto |
| `BS_CRI_FLUXO_LOG`, `BS_CRI_MEDIDORES`, `BS_CRI_SNAP_VB` | passadas com trabalho por VBlank; medidores do CRI por VBlank (B11); RAM do CRI antes e depois de cada passada numa janela |
| `BS_STATE_LOG`, `BS_RAM_MUTACAO` | estado do jogo em checkpoints e mutação de RAM |
| `BS_ARQUIVOS_MUTACAO` | leitura de arquivo corrompida |
| `BS_CLOCK_LOG` | leituras de relógio e estado; escritas (só RAM de som e registradores `0x00700000`–`0x0071FFFF`) e leituras da RAM de som da AICA: o oráculo de áudio |
| `BS_AICA_MUTACAO` | perde a N-ésima escrita na RAM de som (negativo do oráculo de áudio) |
| `BS_TEMPO_MUTACAO` | mutações negativas do provedor de tempo |
| `[vblank] fundidos` | contador de VBlanks fundidos, impresso no fim da execução |

**Esperas em mods (B10):** `bs::mods::wait(c, pc, resume, ciclos)`, `wait_end` e `poll`
(`runtime/mods/mod_api.h`) fazem o que `BS_WAIT`, `BS_WAIT_END` e `BS_POLL` fazem no código
gerado. Um laço de espera reescrito num mod sem `wait()` aparece como falta de progresso, com a
thread e o PC.

**Thread 3 (CRI) e o N2b-2** (`docs/N2b-BLOQUEIOS.md`):
- a passada do servidor é ociosa no gameplay;
- quem a acorda são eventos: VBlank (`8C158A26`), fim de render (`8C158A82`), ações da thread
  principal e, com a D7, o avanço do tempo lógico;
- a única diferença de ordem de um bloqueio até o próximo evento é a cessão de exclusão mútua
  `8C1588AA`, que só existe com preempção no meio da passada.

**Dependências de build (D10):** `BS_MSVC_DEPS_JSON` ligado por padrão.
- O `/sourceDependencies` do MSVC grava JSON, e `tools/build/msvc_srcdeps.cmake` o converte para
  depfile do ninja, independente de idioma e página de código.
- Uma barra invertida no fim de um argumento vira `/`, por causa do `/Fd<pasta>\` do Debug.
- `tools/build` entra no kit do Builder.
- O teste `n2_build_incremental` cobre: página 850, caminho com espaço, `#` e `$$`, Debug e
  falha do compilador.

### Port nativo, N2b-2 experimental: tempo lógico sem ciclos (2026-10-17)

Detalhes em `docs/N2b2-RELATORIO.md` e `docs/N2-PLANO.md` seção 14. Nasceu atrás da opção
`--tempo experimental` e foi promovido a padrão da janela em 2026-10-24, depois de U1–U8 e do H9; o headless e o
recuo explícito continuam em `--tempo nativo`.

**Tempo:** o provedor nativo, com `experimental`, tem um instante lógico próprio. Os ciclos
estimados não o movem. Ele só anda quando o contexto corrente chega a um ponto de espera declarado
(`TimeProvider::wait`, chamado pelo `BS_WAIT`), e só até o próximo evento real. A escolha de thread
continua com o despachante do SDK.

**Pontos de espera:** `[native] esperas` no config, e o tipo de cada um em `kWaitDecls`
(`transitional/time_adapter.cpp`):
- Evento;
- LinhaSpg: próxima troca de linha do feixe;
- Síncrona: dar volta é erro;
- OciosaCri: a thread 3 só espera depois de passada ociosa (`transitional/cri_idle`, com RAM
  rastreada pelo sistema, `MEM_WRITE_WATCH`).

**Erros explícitos:** falta de progresso, deadlock e espera síncrona que dá volta. O tempo nunca
anda sozinho.

**Diagnósticos novos:**
- `BS_FRAME_LOG` (hash do quadro por VBlank);
- `BS_AICA_LOG` e `BS_AICA_LOG_TUDO` (sequência de escritas na AICA, independente da linha do
  tempo);
- `BS_SB_MUTACAO` (esperas síncronas que dão volta);
- mutações `cri_nunca_ocioso`, `cri_sempre_ocioso` e `cri_ignora_ram`.

### Diretriz permanente: mídia moddável no N5 (vídeo) e no N6 (áudio)

Registrada em 2026-10-16. Detalhes em `docs/PLANO-PORT-NATIVO.md`, seção 4.1. Não muda a ordem do
roadmap e nada é implementado antes da etapa autorizada.

**Interface nativa de mídia:**
- recursos por ID lógico e estável, mapeado dos identificadores originais;
- categorias separadas: música, efeito, diálogo e vídeo.

**Formatos:**
- vídeo: MP4/H.264 (com caminho para outros codecs), em qualquer resolução e taxa de quadros;
- áudio: WAV, FLAC e OGG, com loops e transições preservados.

**Mods:**
- substituição em tempo de execução, sem recompilar;
- arquivos maiores que os originais;
- formatos e regras documentados;
- dublagem e legendas como camadas de mídia.

**Contrato com o jogo:**
- a sincronização e os eventos observados pelo jogo (fim de vídeo, posição de stream, estado do
  driver de som) seguem o relógio lógico, independentes do formato;
- **modo fiel** (sem mods ou com mods de mesma duração): o jogo vê o contrato original (eventos,
  durações e estados), igual ao legado;
- **modo modificado** (mídia de outra duração, p.ex. dublagem ou música remasterizada mais
  longa): a duração e os laços vêm dos metadados do recurso do mod. A documentação de cada tipo
  de mídia diz quais eventos do gameplay acompanham a nova duração e quais continuam fixos;
- sem mods, os recursos originais (convertidos pelo Builder a partir do CDI do usuário) são a
  referência de fidelidade.

### N8: corte da simulação do produto

Planejamento técnico concluído em 2026-10-24.
- **N8.0** (portão e referências) concluído em 2026-10-25.
- **N8.1** (N2c) concluído em 2026-10-26 (`docs/N8-1-PREPARACAO.md` seção 7). No tempo lógico (padrão da janela):
  - cadência B: o atendimento é pedido, sem o quantum de ciclos;
  - TMU (`memory/tmu_logical`), CCN não gráfico e cabo (`memory/sys_logical`) e a ponte de interrupções
    (`cpu/irq_bridge`, contrato de `8C00FA00`) servem o jogo pelo contrato observado. Fora dele, os modelos assumem.
  
  O tempo lógico não depende dos ciclos (prova `BS_CICLOS_RUIDO`). O estável por ciclos e os modelos ficam para o
  estável e o recuo até o N8.2 (D-N8.1-A).

- **N8.2** concluído em 2026-10-26 (`docs/N8-2-SEPARACAO.md`):
  - o runtime é montado por papéis (bibliotecas de objetos `bs_rt_*`);
  - o build de **validação** (`BS_VALIDACAO`, preset `msvc-validacao`, Debug, `mods`/`watch`) é o único com as
    mutações e sondas de teste; o **produto** (`msvc-release`, Builder) tem stubs que as recusam;
  - os dados do jogo (`bs_disc_data`) ficam separados da leitura de imagens (`bs_disc`, ferramentas);
  - a catraca `n8_produto_papeis` confere o manifesto contra o mapa do linker.
- **N8.3** (`docs/N8-3-BOOT.md`):
  - o produto não liga BIOS, drive de GD-ROM nem flash simulados;
  - as syscalls do jogo vão ao serviço nativo `system/syscall_service`, nos mesmos endereços e só no contrato medido;
  - fora do contrato, erro explícito;
  - os registradores do drive e o G1 (exceto a escrita `GDST = 0` da tabela do SDK) também são erro explícito;
  - o modelo HLE (`bs_runtime_hle`) fica na validação (`--sistema hle`, `--arquivos legado`, `--flash`,
    `BS_GDROM_TIMING`) e nas ferramentas.
- **N8.4** (`docs/N8-4-PERIFERICOS.md`):
  - o barramento de periféricos do produto é o serviço sem dispositivos `system/maple_bus` (fachada `periph_*`): a
    sondagem da camada per com o mesmo efeito do modelo sem controle;
  - o modelo do Maple (`runtime/maple`), a ponte `input_bridge::install` e `--controles/--saves legado` ficam na
    validação;
  - o soft reset (MISC r4 = 1) encerra a sessão pelo gancho do host.
- **N8.5** (`docs/N8-5-FILMES.md`): os filmes do produto são só os nativos. O `--video legado` (Sofdec) e o conversor
  YUV do TA (`ta_yuv_removed`) são erro explícito no produto. Da biblioteca Sofdec recompilada, só a init roda.
- **N8.6** (`docs/N8-6-AUDIO.md`): a área de som do produto é o serviço `system/sound_bus` (RAM de som, controle e RTC;
  fachada `snd_bus_*`), sem AICA, ARM7 nem driver. O modelo da AICA (`runtime/aica`) fica no `bs_runtime_hle`, e o
  áudio legado, as `BS_AICA_*` e a partida do DMA do G2 são erro explícito no produto.

O inventário, os portões e a ordem N8.0–N8.8 estão em `docs/N8-PLANO.md`.
- **No tempo lógico:** os módulos de Holly/PVR, Maple e GD-ROM continuam medindo e agendando prazos, no relógio lógico
  (`time_now()` devolve o instante lógico), sem usar os ciclos estimados como base.
- **Depois:** o transporte deles e o de DMAC/SQ só sai nos cortes específicos posteriores.

O produto e o alvo de validação terão limites diferentes: o produto deixa de ligar hardware Dreamcast e providers
legados; o alvo de validação preserva simulação, sondas e comparadores enquanto forem necessários. Builder, Recompiler,
Flycast externo, leitura de CDI e ferramentas não são runtime legado do jogo e permanecem. O fallback software também
permanece, mas precisa consumir a mesma IR nativa do Vulkan antes da retirada de TA/VRAM/PVR.

Dois pontos condicionam a implementação: (1) harmonizar o critério literal “sem `Sh4Context`/RAM guest” com a elevação
reservada ao N10; (2) escolher a fonte nativa da IR. A pendência AMD exige preservar o fallback software; a falta de
paridade de fog por tabela/modifier volumes exige preservar captura, raster legado e Flycast até haver evidência.
Essas pendências não bloqueiam N8.0–N8.6; fog/volumes bloqueiam N8.7 e a AMD mantém N7 em 95% sem autorizar retirar o
fallback.

### N9 (planejado): um projeto, duas edições

Planejamento aprovado em 2026-10-23; **não iniciado** (começa depois do N8). Detalhes, critérios e decisões abertas em
`docs/N9-PLANO.md`, seção 2.

- **Blue Stinger Original** e **Blue Stinger Remastered** são duas instalações do **mesmo núcleo C++** (código gerado
  + `native/`: gameplay, tempo, áudio, saves, controles, Vulkan). Sem projeto separado, recompilação do zero, fork
  permanente ou branch por edição.
- **Perfil de edição** (`EditionProfile`, provisório): escolhido pelo Recompiler, gravado em `instalacao.toml`
  (`[edicao]`), lido uma vez e imutável na execução. É separado das **preferências** do jogador (graficos/audio/
  legendas/controles.toml): o perfil define padrões e restrições (`clamp_to`, como as capacidades do renderizador
  hoje), as preferências escolhem dentro delas. Saves e atualizações não mudam o perfil. Perfil não é preset gráfico.
- **Remastered:** câmera Livre obrigatória (as rotinas originais ficam no núcleo para eventos roteirizados e para a
  Original), legendas ligadas em pt-BR, menus e saves de PC (a lógica de VMU continua atrás do cartão nativo do N4),
  gráficos modernos ajustáveis.
- **Camadas:** núcleo (o mesmo) → apresentação (UI nativa, HUD, pós-processamento, iluminação, água, texturas HD,
  widescreen) → preferências → perfil de edição → instalador. O núcleo expõe eventos e estados; a apresentação não
  muda o estado lógico. Onde o núcleo depende da edição (por exemplo, a instalação dos slots da câmera), o ponto é
  documentado e testado nas duas edições.
- Recursos audiovisuais substituíveis sem recompilar (contratos do N5, N6 e N7); APIs e ferramentas de mods no N10;
  nada da camada legada removida no N8 volta; nenhum recurso comercial no repositório público.

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
| D4 | ARM7 do AICA | ~~Interpretador ARM7 no M6~~ **Substituída por D10** (2026-10-15): o produto não executa nem interpreta o ARM7. |
| D5 | Linguagem | **C++20 para tudo** (extrator, tradutor, runtime). Sem Python para o usuário final. |
| D6 | Compilador | Clang-cl como alvo principal e MSVC sempre compilando. Enquanto o clang-cl não estiver instalado, o MSVC é usado. |
| D7 | Distribuição | **Builder** (2026-10-14): o usuário recompila na própria máquina a partir do CDI, com janela Win32 nativa e kit de fontes compilado pelo CMake/Ninja do Visual Studio dele. Ver "Builder" na seção 6. |
| D8 | Port nativo | **`docs/PLANO-PORT-NATIVO.md` v2 aprovado como diretriz** (2026-10-15). Nada de emulação ou interpretação de CPU, BIOS ou dispositivos do Dreamcast na arquitetura final. C++20, Windows x64, editável e moddável. Autorização inicial só para o N0 (`docs/N0-RELATORIO.md`). |
| D9 | GPU | ~~Direct3D 11~~ **Substituída** (2026-10-19): **Vulkan** como renderer principal (N7), atrás da interface independente de backend (`native/render/render.h`, `IRenderDevice` + IR de comandos), nas duas plataformas da D13. Nenhum código Direct3D 11 existia. Ver `docs/DECISAO-VULKAN-LINUX.md`. |
| D10 | Áudio | **XAudio2** (`IAudioOutput`), com reconstrução nativa da lógica do Manatee e dos bancos (`IAudioEngine`). O `MANATEE.DRV` só é analisado estaticamente. |
| D11 | Vídeo | **Media Foundation** para vídeos convertidos pelo Builder (`IVideoPlayer`), desde que formato e fidelidade sejam validados. Senão, apresentar alternativas ao mantenedor. |
| D13 | Plataformas | (2026-10-19) **Windows x64**: desenvolvimento, validação e distribuição atuais. **Linux x86_64**: plataforma futura, sem port agora. C++20 e CMake portáveis, SDL3 para janela, entrada e sistema, Vulkan nas duas. Nenhuma dependência nova de Win32 no gameplay, no código recompilado ou nos contratos compartilhados: APIs de plataforma só em implementações de backend. Media Foundation, XAudio2 e DirectWrite seguem como backends Windows. Auditoria e alternativas em `docs/DECISAO-VULKAN-LINUX.md`. |
| D12 | Módulos | `native/`, `legacy/` e `transitional/` são alvos CMake desde o N0. Os arquivos só mudam de lugar quando o subsistema migrar. A leitura de CDI saiu do produto (`bs_runtime_cdi`). |
| D14 | Edições (N9, planejado) | (2026-10-23) Um projeto e um núcleo C++, **duas edições instaláveis**: Original e Remastered, escolhidas pelo Recompiler por um perfil de edição separado das preferências. Remastered com câmera Livre obrigatória. Sem fork nem branch por edição. Ver "N9 (planejado)" na seção 6 e `docs/N9-PLANO.md`. |

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
