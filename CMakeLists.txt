cmake_minimum_required(VERSION 3.25)
project(BlueStingerRecompiled LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BS_BUILD_TESTS "Compilar testes" ON)
option(BS_BUILD_GAME  "Compilar o executavel do jogo (requer generated/)" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(tools/extractor)
add_subdirectory(translator)
add_subdirectory(runtime)

# Codigo gerado por bs-recomp a partir do executavel do usuario (nao versionado).
set(BS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "Saida do bs-recomp")
if(EXISTS "${BS_GENERATED_DIR}/generated.cmake")
  include("${BS_GENERATED_DIR}/generated.cmake")
  add_library(bs_generated STATIC ${BS_GENERATED_SOURCES})
  target_include_directories(bs_generated PUBLIC "${BS_GENERATED_DIR}")
  target_link_libraries(bs_generated PUBLIC bs_runtime)
  if(MSVC)
    # substitui as opcoes herdadas (/W4) para nao gerar o aviso D9025
    set_property(TARGET bs_generated PROPERTY COMPILE_OPTIONS /bigobj /W1 /utf-8 /permissive-)
  endif()
  add_executable(bs-gen-check tools/gen_check/main.cpp)
  target_link_libraries(bs-gen-check PRIVATE bs_generated)
endif()

if(BS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
