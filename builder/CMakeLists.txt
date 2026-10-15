# Builder: do CDI do usuario ao BlueStinger.exe nativo (extracao, analise, geracao,
# compilacao com o compilador do usuario, verificacao e instalacao). Nao contem nada do jogo.
add_library(bs_builder_core STATIC builder_core.cpp toolchain.cpp child_process.cpp project_export.cpp)
target_include_directories(bs_builder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_builder_core PUBLIC bs_recomp_lib bs_runtime)

add_executable(bs-builder main_cli.cpp builder.manifest)
target_link_libraries(bs-builder PRIVATE bs_builder_core)

# Janela Win32 nativa (controles comuns v6, DPI por monitor; sem dependencias novas)
add_executable(BlueStingerBuilder WIN32 main_win32.cpp builder.manifest)
target_link_libraries(BlueStingerBuilder PRIVATE bs_builder_core comctl32)
