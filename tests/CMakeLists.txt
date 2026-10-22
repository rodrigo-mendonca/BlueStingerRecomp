enable_language(ASM_MASM)
add_executable(bs_unit_tests
  unit/main.cpp
  unit/test_skeleton.cpp
  unit/test_disc.cpp
  unit/test_decoder.cpp
  unit/test_ops.cpp
  unit/test_analysis.cpp
  unit/test_port.cpp
  unit/test_vram.cpp
  unit/test_intc.cpp
  unit/test_analysis_m4.cpp
  unit/test_m4_hw.cpp
  unit/test_threads.cpp
  unit/test_render.cpp
  unit/test_render_formats.cpp
  unit/test_render_capture.cpp
  unit/test_graphics_settings.cpp
  unit/test_subtitle_overlay.cpp
  unit/test_settings_menu.cpp
  unit/test_menu_overlay.cpp
  unit/test_maple.cpp
  unit/test_longjmp.cpp
  unit/test_tmu.cpp
  unit/test_input.cpp
  unit/test_app.cpp
  unit/test_builder.cpp
  unit/test_runtime_data.cpp
  unit/test_mods.cpp
  unit/test_n0.cpp
  unit/test_transitional.cpp
  unit/test_n1.cpp
  unit/test_n2.cpp
  unit/test_pad_contract.cpp
  unit/test_input_native.cpp
  unit/test_camera.cpp   # N7c: camera livre
  unit/test_camera_slots.cpp
  unit/test_saves_native.cpp
  unit/test_bu_machine.cpp
  unit/test_pad_adapter.cpp
  unit/xmm_probe.asm   # XMM6-XMM15 atraves de trocas de fibra (MASM)
)
target_link_libraries(bs_unit_tests PRIVATE bs_translator bs_runtime bs_disc bs_app_core bs_builder_core bs_hwscan bs_transitional)

# N5: unitarios dos filmes (so native/video e a conversao; build leve, sem o runtime)
add_executable(bs_video_tests unit/main.cpp unit/test_video_native.cpp unit/test_video_audio.cpp unit/test_video_subs.cpp unit/test_video_mw.cpp unit/test_video_fixes.cpp unit/test_video_adapter.cpp unit/test_video_build.cpp unit/test_video_mods.cpp unit/test_video_slot.cpp unit/test_video_decoder.cpp)
target_include_directories(bs_video_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(bs_video_tests PRIVATE bs_native_video bs_native_audio bs_video_convert bs_video_mw bs_video_adapter)
add_test(NAME video COMMAND bs_video_tests)

# N6: audio de gameplay portavel, sem o jogo nem dispositivo de audio
add_executable(bs_audio_tests unit/main.cpp unit/test_audio_native.cpp unit/test_audio_sd.cpp unit/test_audio_sosb.cpp
                              unit/test_audio_voice.cpp unit/test_audio_flac.cpp unit/test_audio_ogg.cpp unit/test_audio_manatee.cpp unit/test_audio_manatee_seq.cpp unit/test_audio_manatee_synth.cpp unit/test_audio_manatee_player.cpp unit/test_audio_voice_glue.cpp unit/test_audio_dsp.cpp
                              ${PROJECT_SOURCE_DIR}/transitional/voice_mw_glue.cpp)  # a cola das falas e portavel (sem runtime)
target_include_directories(bs_audio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit ${PROJECT_SOURCE_DIR}/transitional)
target_link_libraries(bs_audio_tests PRIVATE bs_native_audio_core)
add_test(NAME audio COMMAND bs_audio_tests)

# Localizacao: contratos (idiomas, pacotes de mods, textos, legendas novas, layout), so com dados sinteticos
add_executable(bs_localization_tests unit/main.cpp unit/test_localization.cpp)
target_include_directories(bs_localization_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(bs_localization_tests PRIVATE bs_native_localization)
add_test(NAME localization COMMAND bs_localization_tests)

# N7 (preparacao): regras portaveis da renderizacao nativa, sem GPU
add_executable(bs_render_tests unit/main.cpp unit/test_render_native.cpp unit/test_render_textures.cpp unit/test_render_fidelity.cpp unit/test_render_replacement.cpp
               unit/test_render_frame_prep.cpp unit/test_render_vulkan.cpp unit/test_render_post.cpp)
target_include_directories(bs_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(bs_render_tests PRIVATE bs_native_render bs_native_render_vk)
add_test(NAME render COMMAND bs_render_tests)
add_test(NAME unit COMMAND bs_unit_tests)

# Interface do recompilador (catalogo de textos, configuracao da instalacao, troca da instalacao)
add_executable(bs_ui_tests unit/main.cpp unit/test_ui.cpp unit/test_builder.cpp unit/test_dev_inspect.cpp)
target_include_directories(bs_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(bs_ui_tests PRIVATE bs_builder_core bs_builder_ui bs_native tomlplusplus::tomlplusplus)
add_test(NAME ui COMMAND bs_ui_tests)

# Usa a imagem do usuario se BS_DISC_IMAGE estiver definido; senao, SKIP.
add_executable(bs_real_disc_test data/test_real_disc.cpp)
target_link_libraries(bs_real_disc_test PRIVATE bs_disc)
# Raiz do repositorio: BS_DISC_IMAGE relativo (ex.: "ISO/Blue Stinger.cdi") funciona.
add_test(NAME real_disc COMMAND bs_real_disc_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(real_disc PROPERTIES SKIP_RETURN_CODE 77)

# Dados de execucao (o jogo sem o CDI) a partir da imagem real; SKIP sem BS_DISC_IMAGE.
add_executable(bs_runtime_data_test data/test_runtime_data_disc.cpp)
target_link_libraries(bs_runtime_data_test PRIVATE bs_runtime bs_runtime_cdi)
add_test(NAME runtime_data COMMAND bs_runtime_data_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(runtime_data PROPERTIES SKIP_RETURN_CODE 77)

# Analise do executavel real (regressao do NEW GAME); SKIP sem BS_DISC_IMAGE.
add_executable(bs_analysis_real_test data/test_analysis_real.cpp)
target_link_libraries(bs_analysis_real_test PRIVATE bs_recomp_lib bs_runtime bs_runtime_cdi)
add_test(NAME analysis_real COMMAND bs_analysis_real_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(analysis_real PROPERTIES SKIP_RETURN_CODE 77)

# Pipeline completo sobre o programa sintetico: analise -> C++ -> compila -> executa.
# Duas variantes: constantes do pool inline (1) e lidas da RAM (0).
add_executable(bs_fixture_gen codegen/fixture_gen.cpp)
target_link_libraries(bs_fixture_gen PRIVATE bs_translator)
foreach(inline 0 1)
  set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture_gen_${inline}")
  add_custom_command(
    OUTPUT "${gen_dir}/funcs_000.cpp" "${gen_dir}/dispatch.cpp" "${gen_dir}/funcs.h"
    COMMAND bs_fixture_gen "${gen_dir}" ${inline}
    DEPENDS bs_fixture_gen
    COMMENT "Gerando C++ do programa sintetico (inline_pool=${inline})")
  add_executable(bs_codegen_exec_${inline} codegen/test_exec.cpp "${gen_dir}/funcs_000.cpp" "${gen_dir}/dispatch.cpp")
  target_include_directories(bs_codegen_exec_${inline} PRIVATE "${gen_dir}")
  target_link_libraries(bs_codegen_exec_${inline} PRIVATE bs_runtime)
  add_test(NAME codegen_exec_${inline} COMMAND bs_codegen_exec_${inline})
endforeach()

# N0: interfaces nativas compilam sozinhas e nao dependem do Dreamcast.
add_test(NAME n0_native_api COMMAND bs_native_api_check)
add_test(NAME n0_native_limpo COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/n0/check_native_clean.cmake)

# N0: fechamento da fronteira declarada e catraca das metricas sobre o 1ST_READ.BIN real
# (game-data/, de bs-extract); SKIP sem ele. Falha se o jogo passar a chamar o SDK fora de
# config/fronteiras_us.toml, acessar hardware, ou se uma metrica de dependencia piorar.
add_test(NAME n0_fechamento
         COMMAND ${CMAKE_COMMAND} -DHWSCAN=$<TARGET_FILE:bs-hwscan> -DROOT=${PROJECT_SOURCE_DIR}
                 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/n0_hwscan -P ${CMAKE_CURRENT_SOURCE_DIR}/n0/run_fechamento.cmake)
set_tests_properties(n0_fechamento PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")

# N2 (D6): dependencias de cabecalho registradas e build incremental que recompila (MSVC + Ninja).
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
  add_test(NAME n2_build_incremental
           COMMAND ${CMAKE_COMMAND} -DNINJA=${CMAKE_MAKE_PROGRAM} -DBIN=${PROJECT_BINARY_DIR}
                   -DCXX=${CMAKE_CXX_COMPILER} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/n2_incremental
                   -DSRCDEPS=${PROJECT_SOURCE_DIR}/tools/build/msvc_srcdeps.cmake
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/n2/check_build_incremental.cmake)
  set_tests_properties(n2_build_incremental PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
endif()

# N0: a leitura de CDI fica fora do produto (alvo bs_runtime_cdi); conferido no mapa do linker.
if(TARGET bluestinger AND MSVC)
  add_test(NAME n0_produto_sem_cdi
           COMMAND ${CMAKE_COMMAND} -DMAP=$<TARGET_FILE_DIR:bluestinger>/bluestinger.map
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/n0/check_product_map.cmake)
endif()

# Sem janelas modais (assert do CRT de Debug, abort, falha de acesso): o teste falha no stderr em vez de
# esperar um clique e prender o ctest/regressao (tests/sem_dialogos.cpp).
foreach(t bs_unit_tests bs_video_tests bs_ui_tests bs_audio_tests bs_localization_tests bs_real_disc_test bs_runtime_data_test bs_analysis_real_test
          bs_fixture_gen bs_codegen_exec_0 bs_codegen_exec_1)
  target_sources(${t} PRIVATE sem_dialogos.cpp)
endforeach()
# A prova: falha de proposito (assert do vetor no Debug, abort no Release); passa so se a mensagem chegar a saida
# dentro do limite (com a janela modal, estouraria o tempo)
add_executable(bs_sem_dialogos_prova sem_dialogos_prova.cpp sem_dialogos.cpp)
add_test(NAME sem_dialogos COMMAND bs_sem_dialogos_prova)
set_tests_properties(sem_dialogos PROPERTIES TIMEOUT 60
                     PASS_REGULAR_EXPRESSION "vector subscript out of range|sem_dialogos: abort \\(Release\\)")

# Limite de tempo por teste (s), pela duracao medida em 2026-10-20 (com disco; entre parenteses) com folga larga:
# so pega travamento. O preset do ctest da 900 s aos testes sem limite proprio.
set_tests_properties(unit video ui PROPERTIES TIMEOUT 300)                   # (6,7 s / 2,7 s / 5,2 s)
set_tests_properties(audio localization PROPERTIES TIMEOUT 300)                         # (< 1 s)
set_tests_properties(real_disc codegen_exec_0 codegen_exec_1 n0_native_api n0_native_limpo PROPERTIES TIMEOUT 120)  # (< 0,2 s)
set_tests_properties(analysis_real PROPERTIES TIMEOUT 300)                   # (2,9 s)
set_tests_properties(runtime_data n0_fechamento PROPERTIES TIMEOUT 600)      # (23,8 s / 20,0 s)
if(TEST n2_build_incremental)
  set_tests_properties(n2_build_incremental PROPERTIES TIMEOUT 900)          # (6,2 s; compila: folga para cache frio)
endif()
if(TEST n0_produto_sem_cdi)
  set_tests_properties(n0_produto_sem_cdi PROPERTIES TIMEOUT 120)            # (0,2 s)
endif()
