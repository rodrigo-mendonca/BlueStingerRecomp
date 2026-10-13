add_library(bs_runtime STATIC
  cpu/sh4_context.cpp
  cpu/sh4_runtime.cpp
  memory/memory.cpp
  memory/sh4_port.cpp
  memory/sh4_ccn.cpp
  pvr/pvr_regs.cpp
  pvr/holly_intc.cpp
  pvr/holly_sb.cpp
  debug/snapshot.cpp
)
target_include_directories(bs_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BS_BUILD_GAME)
  # M4: SDL3 + Vulkan + generated/*.cpp
  message(FATAL_ERROR "BS_BUILD_GAME ainda nao suportado (M4)")
endif()
