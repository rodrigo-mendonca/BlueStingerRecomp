# N8.2: o runtime por papel (docs/N8-2-SEPARACAO.md). Cada papel e uma biblioteca de objetos; o bs_runtime junta os
# papeis do produto. O papel de validacao (mutacoes e sondas exclusivas de teste) so entra com BS_VALIDACAO; sem ela o
# lugar e dos stubs que recusam a variavel de ambiente correspondente. O grafo vai para runtime_papeis.txt (catraca
# n8_produto_papeis).
set(BS_RT_PAPEIS)

# substrato AOT (L1-L3, L7, L21: ate o N10): contexto, despacho, servico e cadencia, fibras, RAM e roteamento, mods
add_library(bs_rt_aot OBJECT
  cpu/sh4_context.cpp
  cpu/sh4_runtime.cpp
  cpu/sh4_threads.cpp
  memory/memory.cpp
  mods/mod_api.cpp
)
list(APPEND BS_RT_PAPEIS aot)

# servicos nativos: tempo logico (N8.1: ponte de interrupcoes, TMU, CCN nao grafico e cabo) e syscalls do sistema
# (N8.3: GD-ROM, flash e sysinfo no contrato do jogo, sem BIOS, drive nem flash simulados)
add_library(bs_rt_nativo OBJECT
  cpu/irq_bridge.cpp
  memory/tmu_logical.cpp
  memory/sys_logical.cpp
  system/syscall_service.cpp
  system/maple_bus.cpp
  system/sound_bus.cpp
)
# N8.7 (docs/N8-7-KAMUI.md, incremento 3a): no produto, os registradores graficos sao o estado do quadro nativo; na
# validacao o banco do legado (pvr/pvr_regs.cpp) continua ativo e o FrameState roda em sombra
if(NOT BS_VALIDACAO)
  target_sources(bs_rt_nativo PRIVATE system/graphics_bus.cpp)
endif()
list(APPEND BS_RT_PAPEIS nativo)

# simulacao do Dreamcast ainda no caminho do produto (compartilhada ate os cortes N8.4-N8.7): boot (estado de entrada
# medido), Maple e entrada, AICA, PVR/Holly/TA/VRAM, store queues e DMAC
add_library(bs_rt_simulacao OBJECT
  memory/sh4_sq.cpp
  memory/sh4_dmac.cpp
  pvr/pvr_render.cpp
  pvr/pvr_video.cpp
  pvr/pvr_texture.cpp
  pvr/holly_intc.cpp
  pvr/holly_sb.cpp
  pvr/vram.cpp
  pvr/ta.cpp
  input/input.cpp
  boot/native_boot.cpp
)
list(APPEND BS_RT_PAPEIS simulacao)

# tempo estavel por ciclos e modelos de recuo (compartilhado: D-N8.1-A, --tempo nativo/legado, headless historico e o
# recuo dos servicos do N8.1 fora do contrato)
add_library(bs_rt_estavel OBJECT
  cpu/time_legacy.cpp
  cpu/sh4_intc.cpp
  memory/sh4_tmu.cpp
  memory/sh4_ccn.cpp
  memory/sh4_port.cpp
)
list(APPEND BS_RT_PAPEIS estavel)

# instrumentacao das referencias (compartilhada: inerte sem a variavel; as 16 referencias conferem o exe do produto)
add_library(bs_rt_diagnostico OBJECT
  debug/snapshot.cpp
  debug/event_trace.cpp
  debug/intc_trace.cpp
  debug/call_trace.cpp
  debug/file_log.cpp
  debug/clock_log.cpp
)
list(APPEND BS_RT_PAPEIS diagnostico)

# exclusivo de validacao: ruido de ciclos, sonda de hardware do N0, diagnostico do CRI
if(BS_VALIDACAO)
  # N8.7: a referencia grafica do caminho do TA: o rasterizador sobre o TA, os texels da VRAM e o conversor YUV
  add_library(bs_rt_validacao OBJECT debug/hw_probe.cpp debug/gfx_probe.cpp debug/cycle_noise.cpp debug/cri_diag.cpp
    pvr/pvr_render_ta.cpp pvr/pvr_texel.cpp pvr/ta_yuv.cpp
    pvr/pvr_regs.cpp)  # N8.7 3a: o banco de registradores do legado, referencia do FrameState
  list(APPEND BS_RT_PAPEIS validacao)
else()
  add_library(bs_rt_validacao OBJECT debug/validacao_stubs.cpp)
  list(APPEND BS_RT_PAPEIS validacao_stubs)
endif()

# N8.3: modelo HLE do BIOS, do drive de GD-ROM e da flash (o provedor legado de arquivos por setores, --flash e o tempo
# reios). Fora do bs_runtime: o produto nao o liga; a validacao (bs_app_core com BS_VALIDACAO) e as ferramentas
# (bs_runtime_cdi: Builder, bs-diff, testes com o CDI) sim.
add_library(bs_rt_hle OBJECT hle/bios.cpp hle/flashrom.cpp gdrom/gdrom.cpp gdrom/data_source.cpp
  maple/maple.cpp  # N8.4: o modelo do Maple (controle no barramento) tambem
  aica/aica_regs.cpp)  # N8.6: e o da AICA (RAM de som, controle, RTC e os diagnosticos BS_AICA_*)
if(BS_VALIDACAO)
  list(APPEND BS_RT_PAPEIS hle)
endif()

set(BS_RT_OBJ bs_rt_aot bs_rt_nativo bs_rt_simulacao bs_rt_estavel bs_rt_diagnostico bs_rt_validacao)
foreach(o ${BS_RT_OBJ} bs_rt_hle)
  target_include_directories(${o} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # dados do jogo (RuntimeDisc, IP.BIN, descrambler) e o mapa de regioes; o leitor de CDI fica fora (bs_disc)
  # N8.7: o rasterizador em software e nativo (native/render/soft); o caminho do TA o usa com os texels da VRAM
  target_link_libraries(${o} PUBLIC bs_disc_data bs_native_render PRIVATE xinput bs_dcmap)
endforeach()

add_library(bs_runtime STATIC
  $<TARGET_OBJECTS:bs_rt_aot> $<TARGET_OBJECTS:bs_rt_nativo> $<TARGET_OBJECTS:bs_rt_simulacao>
  $<TARGET_OBJECTS:bs_rt_estavel> $<TARGET_OBJECTS:bs_rt_diagnostico> $<TARGET_OBJECTS:bs_rt_validacao>)
target_include_directories(bs_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_runtime PUBLIC bs_disc_data bs_native_render PRIVATE xinput bs_dcmap)
string(REPLACE ";" "\n" BS_RT_PAPEIS_TXT "${BS_RT_PAPEIS}")
file(WRITE ${CMAKE_BINARY_DIR}/runtime_papeis.txt "${BS_RT_PAPEIS_TXT}\n")
# manifesto canonico papel<TAB>fonte, das mesmas listas dos alvos (catraca n8_produto_papeis; revisao Codex 2026-10-26)
set(_manifesto "")
set(_manifesto_obj ${BS_RT_OBJ})
if(BS_VALIDACAO)
  list(APPEND _manifesto_obj bs_rt_hle)  # o executavel de validacao liga o modelo HLE
endif()
foreach(o ${_manifesto_obj})
  string(REPLACE "bs_rt_" "" _papel "${o}")
  if(_papel STREQUAL "validacao" AND NOT BS_VALIDACAO)
    set(_papel "validacao_stubs")
  endif()
  get_target_property(_fontes ${o} SOURCES)
  foreach(f ${_fontes})
    string(APPEND _manifesto "${_papel}\t${f}\n")
  endforeach()
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/runtime_manifesto.tsv "${_manifesto}")

# Leitura direta do CDI (setores e boot), fora do produto: Builder, bs-diff e testes com a
# imagem real. O executavel do jogo nao liga este alvo (teste n0_produto_sem_cdi).
add_library(bs_runtime_hle STATIC $<TARGET_OBJECTS:bs_rt_hle>)
target_include_directories(bs_runtime_hle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_runtime_hle PUBLIC bs_runtime)

add_library(bs_runtime_cdi STATIC gdrom/image_source.cpp boot/disc_boot.cpp)
target_link_libraries(bs_runtime_cdi PUBLIC bs_runtime bs_runtime_hle bs_disc)

# O executavel do jogo (BS_BUILD_GAME) fica em app/: o runtime nao depende de SDL3.
