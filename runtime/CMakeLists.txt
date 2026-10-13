add_library(bs_runtime STATIC
  cpu/sh4_context.cpp
)
target_include_directories(bs_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BS_BUILD_GAME)
  # M4: SDL3 + Vulkan + generated/*.cpp
  message(FATAL_ERROR "BS_BUILD_GAME ainda nao suportado (M4)")
endif()
