# Sistemas nativos definitivos (docs/PLANO-PORT-NATIVO.md). C++20 puro, sem Sh4Context,
# enderecos do guest ou registradores (conferido por n0_native_limpo).
add_library(bs_native_api INTERFACE)
target_include_directories(bs_native_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_native_api INTERFACE cxx_std_20)

# Implementacoes nativas (produto). N1: arquivos do jogo sobre a pasta de recursos. N2a: relogio
# logico e fila de eventos.
add_library(bs_native STATIC platform/data_files.cpp platform/clock.cpp platform/event_queue.cpp)
target_link_libraries(bs_native PUBLIC bs_native_api)

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  # Compila todos os cabecalhos so com native/ no include path: nada do legado pode vazar.
  add_executable(bs_native_api_check ${CMAKE_SOURCE_DIR}/tests/n0/native_api_check.cpp)
  target_link_libraries(bs_native_api_check PRIVATE bs_native)
endif()
