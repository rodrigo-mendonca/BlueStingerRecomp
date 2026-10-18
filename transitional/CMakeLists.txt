# Interoperabilidade da fase hibrida (docs/PLANO-PORT-NATIVO.md secao 2): handles de 32 bits,
# adaptadores Sh4Context <-> native/, registro de fronteiras com provedor legado/nativo.
# Some quando os chamadores forem elevados.
add_library(bs_transitional STATIC boundary_registry.cpp cri_idle.cpp gdfs_adapter.cpp input_bridge.cpp pad_contract.cpp pad_probe.cpp
            time_adapter.cpp)
target_include_directories(bs_transitional PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_transitional PUBLIC bs_runtime bs_native bs_dcmap)
