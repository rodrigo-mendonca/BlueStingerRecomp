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

# servicos nativos do tempo logico (N8.1): ponte de interrupcoes, TMU, CCN nao grafico e cabo
add_library(bs_rt_nativo OBJECT
  cpu/irq_bridge.cpp
  memory/tmu_logical.cpp
  memory/sys_logical.cpp
)
list(APPEND BS_RT_PAPEIS nativo)

# simulacao do Dreamcast ainda no caminho do produto (compartilhada ate os cortes N8.3-N8.7): GD-ROM, BIOS/flash HLE,
# boot, Maple e entrada, AICA, PVR/Holly/TA/VRAM, store queues e DMAC
add_library(bs_rt_simulacao OBJECT
  memory/sh4_sq.cpp
  memory/sh4_dmac.cpp
  pvr/pvr_regs.cpp
  pvr/pvr_render.cpp
  pvr/pvr_video.cpp
  pvr/pvr_texture.cpp
  pvr/holly_intc.cpp
  pvr/holly_sb.cpp
  pvr/vram.cpp
  pvr/ta.cpp
  maple/maple.cpp
  input/input.cpp
  boot/native_boot.cpp
  aica/aica_regs.cpp
  hle/bios.cpp
  hle/flashrom.cpp
  gdrom/gdrom.cpp
  gdrom/data_source.cpp
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
  add_library(bs_rt_validacao OBJECT debug/hw_probe.cpp debug/cycle_noise.cpp debug/cri_diag.cpp)
  list(APPEND BS_RT_PAPEIS validacao)
else()
  add_library(bs_rt_validacao OBJECT debug/validacao_stubs.cpp)
  list(APPEND BS_RT_PAPEIS validacao_stubs)
endif()

set(BS_RT_OBJ bs_rt_aot bs_rt_nativo bs_rt_simulacao bs_rt_estavel bs_rt_diagnostico bs_rt_validacao)
foreach(o ${BS_RT_OBJ})
  target_include_directories(${o} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # dados do jogo (RuntimeDisc, IP.BIN, descrambler) e o mapa de regioes; o leitor de CDI fica fora (bs_disc)
  target_link_libraries(${o} PUBLIC bs_disc_data PRIVATE xinput bs_dcmap)
endforeach()

add_library(bs_runtime STATIC
  $<TARGET_OBJECTS:bs_rt_aot> $<TARGET_OBJECTS:bs_rt_nativo> $<TARGET_OBJECTS:bs_rt_simulacao>
  $<TARGET_OBJECTS:bs_rt_estavel> $<TARGET_OBJECTS:bs_rt_diagnostico> $<TARGET_OBJECTS:bs_rt_validacao>)
target_include_directories(bs_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_runtime PUBLIC bs_disc_data PRIVATE xinput bs_dcmap)
string(REPLACE ";" "\n" BS_RT_PAPEIS_TXT "${BS_RT_PAPEIS}")
file(WRITE ${CMAKE_BINARY_DIR}/runtime_papeis.txt "${BS_RT_PAPEIS_TXT}\n")
# manifesto canonico papel<TAB>fonte, das mesmas listas dos alvos (catraca n8_produto_papeis; revisao Codex 2026-10-26)
set(_manifesto "")
foreach(o ${BS_RT_OBJ})
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
add_library(bs_runtime_cdi STATIC gdrom/image_source.cpp boot/disc_boot.cpp)
target_link_libraries(bs_runtime_cdi PUBLIC bs_runtime bs_disc)

# O executavel do jogo (BS_BUILD_GAME) fica em app/: o runtime nao depende de SDL3.
