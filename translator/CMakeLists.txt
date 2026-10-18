# toml++: declarado no CMakeLists.txt da raiz (o produto tambem usa, N3-1)

# Biblioteca compartilhada por analisador, gerador e testes.
add_library(bs_translator STATIC
  decoder/sh4_decoder.cpp
  analysis/analysis.cpp
  codegen/codegen.cpp
)
target_include_directories(bs_translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Analise + geracao como biblioteca: bs-recomp e o Builder.
add_library(bs_recomp_lib STATIC app/config.cpp app/recomp.cpp)
target_include_directories(bs_recomp_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_recomp_lib PUBLIC bs_translator PRIVATE tomlplusplus::tomlplusplus)

add_executable(bs-recomp app/main.cpp)
target_link_libraries(bs-recomp PRIVATE bs_recomp_lib)

add_executable(bs-disasm app/disasm_main.cpp)
target_link_libraries(bs-disasm PRIVATE bs_translator)

# N0: varredura de dependencias de hardware (manifesto, grafo, bibliotecas, metricas).
add_library(bs_hwscan STATIC
  hwscan/hwscan.cpp
  hwscan/sdkid.cpp
  hwscan/closure.cpp
  hwscan/metrics.cpp
  hwscan/boundary_io.cpp
  hwscan/pipeline.cpp
  hwscan/probe.cpp
)
target_include_directories(bs_hwscan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/hwscan)
target_link_libraries(bs_hwscan PUBLIC bs_translator bs_dcmap bs_recomp_lib PRIVATE tomlplusplus::tomlplusplus)

add_executable(bs-hwscan hwscan/main.cpp)
target_link_libraries(bs-hwscan PRIVATE bs_hwscan)
