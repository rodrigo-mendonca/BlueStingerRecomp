# Builder: do CDI do usuario ao BlueStinger.exe nativo (extracao, analise, geracao,
# compilacao com o compilador do usuario, verificacao e instalacao). Nao contem nada do jogo.
# N5: leitura dos filmes do disco (demux do SFD e ADX) para a conversao em MP4
add_library(bs_video_convert STATIC video/sfd_media.cpp video/sfd_convert.cpp video/movie_build.cpp video/pl_mpeg_impl.c)
target_include_directories(bs_video_convert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg)
target_link_libraries(bs_video_convert PUBLIC bs_native_video PRIVATE bs_disc tomlplusplus::tomlplusplus)
target_compile_features(bs_video_convert PUBLIC cxx_std_20)
add_executable(bs-video-probe video/probe_main.cpp)  # desenvolvimento: confere os SFD do disco
target_link_libraries(bs-video-probe PRIVATE bs_video_convert bs_disc)
target_include_directories(bs-video-probe PRIVATE ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg)

add_library(bs_builder_core STATIC builder_core.cpp toolchain.cpp child_process.cpp project_export.cpp)
target_include_directories(bs_builder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_builder_core PUBLIC bs_recomp_lib bs_runtime bs_runtime_cdi PRIVATE bs_video_convert bs_native)

add_executable(bs-builder main_cli.cpp builder.manifest)
target_link_libraries(bs-builder PRIVATE bs_builder_core)

# Textos da interface simples por chave (pt-BR, en-US; um arquivo por idioma)
add_library(bs_builder_ui STATIC ui/i18n.cpp ui/lang_pt_br.cpp ui/lang_en_us.cpp)
target_include_directories(bs_builder_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_builder_ui PUBLIC cxx_std_20)

# Janelas Win32 nativas (controles comuns v6, DPI por monitor), sobre o mesmo bs_builder_core:
# - BlueStinger-Recompiler.exe: usuario final (CDI, pasta, idioma, modo portatil; mensagens traduzidas);
# - BlueStinger-Developer.exe: projeto C++ editavel, recompilar do zero e log tecnico.
add_executable(BlueStinger-Recompiler WIN32 main_recompiler.cpp builder.manifest)
target_link_libraries(BlueStinger-Recompiler PRIVATE bs_builder_core bs_builder_ui bs_native comctl32)
add_executable(BlueStinger-Developer WIN32 main_developer.cpp builder.manifest)
target_link_libraries(BlueStinger-Developer PRIVATE bs_builder_core comctl32)
