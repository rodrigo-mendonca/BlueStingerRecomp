# Biblioteca compartilhada por analisador, gerador e testes.
add_library(bs_translator STATIC
  decoder/sh4_decoder.cpp
)
target_include_directories(bs_translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bs-recomp app/main.cpp)
target_link_libraries(bs-recomp PRIVATE bs_translator)
