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

if(BS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
