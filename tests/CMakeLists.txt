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
  unit/test_pad_adapter.cpp
  unit/xmm_probe.asm   # XMM6-XMM15 atraves de trocas de fibra (MASM)
)
target_link_libraries(bs_unit_tests PRIVATE bs_translator bs_runtime bs_disc bs_app_core bs_builder_core bs_hwscan bs_transitional)
add_test(NAME unit COMMAND bs_unit_tests)

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
