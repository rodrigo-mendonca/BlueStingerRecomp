# Mapa de enderecos do Dreamcast (N0): usado pelo scanner estatico, pela sonda do runtime
# legado e pelas metricas. Sem dependencias.
add_library(bs_dcmap STATIC dc_regions.cpp)
target_include_directories(bs_dcmap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
