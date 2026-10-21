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
# OGG/Vorbis (third_party/xiph/README.md): so a decodificacao do libogg 1.3.6 e do libvorbis 1.3.7, compilada aqui (sem
# o CMake deles; nada baixado). Codigo C de terceiros sem alteracoes: os avisos dele nao valem para o projeto.
set(BS_XIPH ${PROJECT_SOURCE_DIR}/third_party/xiph)
add_library(bs_xiph_vorbis STATIC ${BS_XIPH}/libogg/src/bitwise.c ${BS_XIPH}/libogg/src/framing.c
            ${BS_XIPH}/libvorbis/lib/mdct.c ${BS_XIPH}/libvorbis/lib/smallft.c ${BS_XIPH}/libvorbis/lib/block.c
            ${BS_XIPH}/libvorbis/lib/envelope.c ${BS_XIPH}/libvorbis/lib/window.c ${BS_XIPH}/libvorbis/lib/lsp.c
            ${BS_XIPH}/libvorbis/lib/lpc.c ${BS_XIPH}/libvorbis/lib/analysis.c ${BS_XIPH}/libvorbis/lib/synthesis.c
            ${BS_XIPH}/libvorbis/lib/psy.c ${BS_XIPH}/libvorbis/lib/info.c ${BS_XIPH}/libvorbis/lib/floor1.c
            ${BS_XIPH}/libvorbis/lib/floor0.c ${BS_XIPH}/libvorbis/lib/res0.c ${BS_XIPH}/libvorbis/lib/mapping0.c
            ${BS_XIPH}/libvorbis/lib/registry.c ${BS_XIPH}/libvorbis/lib/codebook.c ${BS_XIPH}/libvorbis/lib/sharedbook.c
            ${BS_XIPH}/libvorbis/lib/lookup.c ${BS_XIPH}/libvorbis/lib/bitrate.c ${BS_XIPH}/libvorbis/lib/vorbisfile.c)
target_include_directories(bs_xiph_vorbis PUBLIC ${BS_XIPH}/libogg/include ${BS_XIPH}/libvorbis/include
                           PRIVATE ${BS_XIPH}/libvorbis/lib)
if(MSVC)
  target_compile_options(bs_xiph_vorbis PRIVATE /W0)
  target_compile_definitions(bs_xiph_vorbis PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(bs_xiph_vorbis PRIVATE -w)
endif()

# N6: audio de gameplay portavel (ADPCM da AICA, mixer, catalogo com mods, leitura de arquivos); sem
# Win32 - o backend de saida (XAudio2 hoje, SDL3 no futuro) fica no bs_native_audio
add_library(bs_native_audio_core STATIC audio/aica_adpcm.cpp audio/mixer.cpp audio/sound_catalog.cpp audio/sound_file.cpp
            audio/sd_adapter.cpp audio/sosb.cpp  # adaptador das 5 entradas do SDK; efeitos do BS_SND.MLT (fase C)
            audio/voice_player.cpp               # fase E: falas (MW WAV) no tempo logico, com dublagem
            audio/sound_flac.cpp                 # FLAC de mods (dr_flac, third_party/dr_libs)
            audio/sound_vorbis.cpp               # OGG/Vorbis de mods (libogg + libvorbis, third_party/xiph)
            audio/manatee.cpp                    # fase F: sequencias e bancos do Manatee (M*.MSB/MPB)
            audio/manatee_seq.cpp                # fase F: o sequenciador (a agenda do driver, 250 Hz)
            audio/manatee_synth.cpp              # fase F: o sintetizador (camadas, tom do driver, mixer)
            audio/manatee_player.cpp)            # fase F: slots, portas e faders do driver (o lado que o SDK comanda)
# so o sound_flac.cpp inclui o dr_flac; os avisos do cabecalho de terceiros nao entram como erro
target_include_directories(bs_native_audio_core PRIVATE ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
target_link_libraries(bs_native_audio_core PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus bs_xiph_vorbis)

# Localizacao (docs/LOCALIZACAO-PLANO.md): contratos portaveis de textos traduzidos, legendas novas (filmes e
# falas) e pacotes de mods; o parser SRT/WebVTT e o do N5 (bs_native_video). Sem desenho (N7) e sem ferramentas (N9).
add_library(bs_native_localization STATIC localization/locale.cpp localization/mod_packages.cpp
            localization/text_localization.cpp localization/subtitle_service.cpp localization/subtitle_presenter.cpp
            localization/dialogue_bridge.cpp)  # N6 fase E: eventos das falas -> legendas
target_link_libraries(bs_native_localization PUBLIC bs_native_api bs_native_audio_core PRIVATE bs_native_video tomlplusplus::tomlplusplus)

add_library(bs_native_audio STATIC audio/audio_output.cpp)
target_link_libraries(bs_native_audio PUBLIC bs_native_api PRIVATE xaudio2 ole32)

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  # Compila todos os cabecalhos so com native/ no include path: nada do legado pode vazar.
  add_executable(bs_native_api_check ${CMAKE_SOURCE_DIR}/tests/n0/native_api_check.cpp)
  target_link_libraries(bs_native_api_check PRIVATE bs_native)
endif()
