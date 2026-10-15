add_library(bs_runtime STATIC
  cpu/sh4_context.cpp
  cpu/sh4_runtime.cpp
  cpu/sh4_intc.cpp
  cpu/sh4_threads.cpp
  memory/memory.cpp
  memory/sh4_port.cpp
  memory/sh4_tmu.cpp
  memory/sh4_ccn.cpp
  memory/sh4_sq.cpp
  memory/sh4_dmac.cpp
  pvr/pvr_regs.cpp
  pvr/pvr_render.cpp
  pvr/pvr_video.cpp
  pvr/pvr_texture.cpp
  pvr/holly_intc.cpp
  pvr/holly_sb.cpp
  maple/maple.cpp
  input/input.cpp
  boot/native_boot.cpp
  pvr/vram.cpp
  pvr/ta.cpp
  aica/aica_regs.cpp
  hle/bios.cpp
  hle/flashrom.cpp
  gdrom/gdrom.cpp
  gdrom/data_source.cpp
  mods/mod_api.cpp
  debug/snapshot.cpp
  debug/hw_probe.cpp
  debug/event_trace.cpp
)
target_include_directories(bs_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# leitura da imagem de disco do usuario (gdrom/image_source.cpp)
target_link_libraries(bs_runtime PUBLIC bs_disc PRIVATE xinput bs_dcmap)

# Leitura direta do CDI (setores e boot), fora do produto: Builder, bs-diff e testes com a
# imagem real. O executavel do jogo nao liga este alvo (teste n0_produto_sem_cdi).
add_library(bs_runtime_cdi STATIC gdrom/image_source.cpp boot/disc_boot.cpp)
target_link_libraries(bs_runtime_cdi PUBLIC bs_runtime bs_disc)

# O executavel do jogo (BS_BUILD_GAME) fica em app/: o runtime nao depende de SDL3.
