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
  unit/xmm_probe.asm   # XMM6-XMM15 atraves de trocas de fibra (MASM)
)
target_link_libraries(bs_unit_tests PRIVATE bs_translator bs_runtime bs_disc bs_app_core bs_builder_core)
add_test(NAME unit COMMAND bs_unit_tests)

# Usa a imagem do usuario se BS_DISC_IMAGE estiver definido; senao, SKIP.
add_executable(bs_real_disc_test data/test_real_disc.cpp)
target_link_libraries(bs_real_disc_test PRIVATE bs_disc)
# Raiz do repositorio: BS_DISC_IMAGE relativo (ex.: "ISO/Blue Stinger.cdi") funciona.
add_test(NAME real_disc COMMAND bs_real_disc_test WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(real_disc PROPERTIES SKIP_RETURN_CODE 77)

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
