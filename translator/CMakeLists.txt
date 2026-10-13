include(FetchContent)
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(tomlplusplus)

# Biblioteca compartilhada por analisador, gerador e testes.
add_library(bs_translator STATIC
  decoder/sh4_decoder.cpp
  analysis/analysis.cpp
  codegen/codegen.cpp
)
target_include_directories(bs_translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bs-recomp app/main.cpp app/config.cpp)
target_link_libraries(bs-recomp PRIVATE bs_translator tomlplusplus::tomlplusplus)

add_executable(bs-disasm app/disasm_main.cpp)
target_link_libraries(bs-disasm PRIVATE bs_translator)
