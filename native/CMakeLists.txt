# Sistemas nativos definitivos (docs/PLANO-PORT-NATIVO.md). C++20 puro, sem Sh4Context,
# enderecos do guest ou registradores (conferido por n0_native_limpo).
add_library(bs_native_api INTERFACE)
target_include_directories(bs_native_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_native_api INTERFACE cxx_std_20)

# Implementacoes nativas (produto). N1: arquivos do jogo sobre a pasta de recursos. N2a: relogio
# logico e fila de eventos. N3-1: nucleo de entrada (controles, mapa de acoes em TOML, XInput de reserva).
# N4: saves (formato VMS/envelope e armazenamento atomico em disco).
add_library(bs_native STATIC platform/data_files.cpp platform/clock.cpp platform/event_queue.cpp
            platform/progress.cpp platform/input_map.cpp platform/input_system.cpp
            platform/saves_format.cpp platform/saves_store.cpp platform/install_config.cpp)
target_link_libraries(bs_native PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus xinput)

# N5: filmes nativos (catalogo por ID com mods, leitura e gravacao MP4 por Media Foundation)
add_library(bs_native_video STATIC video/movie_catalog.cpp video/mf_movie.cpp video/movie_playback.cpp video/subtitles.cpp video/movie_color.cpp)
target_link_libraries(bs_native_video PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus mfplat mfreadwrite mfuuid ole32 d2d1 dwrite windowscodecs)
# N5 (D2): saida de audio (XAudio2 e nula) atras de IAudioOutput; o N6 reaproveita
add_library(bs_native_audio STATIC audio/audio_output.cpp)
target_link_libraries(bs_native_audio PUBLIC bs_native_api PRIVATE xaudio2 ole32)

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  # Compila todos os cabecalhos so com native/ no include path: nada do legado pode vazar.
  add_executable(bs_native_api_check ${CMAKE_SOURCE_DIR}/tests/n0/native_api_check.cpp)
  target_link_libraries(bs_native_api_check PRIVATE bs_native)
endif()
