# Interoperabilidade da fase hibrida (docs/PLANO-PORT-NATIVO.md secao 2): handles de 32 bits,
# adaptadores Sh4Context <-> native/, registro de fronteiras com provedor legado/nativo.
# Some quando os chamadores forem elevados.
add_library(bs_transitional STATIC boundary_registry.cpp bu_adapter.cpp bu_machine.cpp cri_idle.cpp gdfs_adapter.cpp input_bridge.cpp
            pad_adapter.cpp pad_contract.cpp pad_probe.cpp time_adapter.cpp
            render_capture.cpp   # N7a: IR de render a partir do decodificador do TA
            free_camera.cpp)     # N7c: camera livre (slots da montagem da camera de gameplay)
target_include_directories(bs_transitional PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_transitional PUBLIC bs_runtime bs_native bs_native_render bs_native_render_vk bs_dcmap)

# N5: nucleo do provedor nativo das entradas MW dos filmes (sem Sh4Context; a cola fica no adaptador)
add_library(bs_video_mw STATIC video_mw_core.cpp video_mw_glue.cpp)
target_include_directories(bs_video_mw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_video_mw PUBLIC bs_native_video bs_native_audio)

# N5: slots das entradas MW sobre o nucleo acima (Sh4Context, registro de fronteiras, mods)
add_library(bs_video_adapter STATIC video_adapter.cpp)
target_link_libraries(bs_video_adapter PUBLIC bs_transitional bs_video_mw)

# N6 fase D: slots das 5 entradas do SDK de som sobre o SdAdapter e o mixer nativo (efeitos do BS_SND.MLT)
add_library(bs_sound_adapter STATIC sound_adapter.cpp voice_mw_glue.cpp)
target_link_libraries(bs_sound_adapter PUBLIC bs_transitional bs_native_audio_core bs_native_audio)
