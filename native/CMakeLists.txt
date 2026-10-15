# Sistemas nativos definitivos (docs/PLANO-PORT-NATIVO.md). No N0 so as interfaces: C++20
# puro, sem Sh4Context, enderecos do guest ou registradores (conferido por n0_native_limpo).
add_library(bs_native_api INTERFACE)
target_include_directories(bs_native_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_native_api INTERFACE cxx_std_20)

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  # Compila todos os cabecalhos so com native/ no include path: nada do legado pode vazar.
  add_executable(bs_native_api_check ${CMAKE_SOURCE_DIR}/tests/n0/native_api_check.cpp)
  target_link_libraries(bs_native_api_check PRIVATE bs_native_api)
endif()
