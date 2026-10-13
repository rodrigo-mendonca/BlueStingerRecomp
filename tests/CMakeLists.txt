add_executable(bs_unit_tests
  unit/main.cpp
  unit/test_skeleton.cpp
  unit/test_disc.cpp
)
target_link_libraries(bs_unit_tests PRIVATE bs_translator bs_runtime bs_disc)
add_test(NAME unit COMMAND bs_unit_tests)

# Usa a imagem do usuario se BS_DISC_IMAGE estiver definido; senao, SKIP.
add_executable(bs_real_disc_test data/test_real_disc.cpp)
target_link_libraries(bs_real_disc_test PRIVATE bs_disc)
add_test(NAME real_disc COMMAND bs_real_disc_test)
set_tests_properties(real_disc PROPERTIES SKIP_RETURN_CODE 77)
