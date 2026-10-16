cmake_minimum_required(VERSION 3.25)
project(BlueStingerRecompiled LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BS_BUILD_TESTS "Compilar testes" ON)
option(BS_BUILD_GAME  "Compilar o executavel do jogo (requer generated/)" OFF)
# OFF no kit do Builder: so runtime + jogo (sem tradutor, ferramentas de dev nem rede)
option(BS_BUILD_TOOLS "Compilar tradutor, ferramentas de desenvolvimento e o Builder" ON)
# Projeto editavel (mods): chamadas entre funcoes geradas passam por slots substituiveis e
# os .cpp de BS_MODS_DIR entram no executavel (runtime/mods/mod_api.h).
option(BS_MODS "Compilar com a camada de mods" OFF)
set(BS_MODS_DIR "" CACHE PATH "Pasta com os mods (.cpp) do projeto editavel")

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  # Ninja acha as dependencias de cabecalho pelo prefixo do /showIncludes, que o cl traduz.
  # Com o prefixo localizado, a codificacao muda com a pagina de codigo do terminal e um
  # build de outro terminal perde as dependencias (objetos velhos: STATUS.md, N1). Os
  # presets pedem ingles (VSLANG=1033); sem o pacote de idioma ingles do VS, compile sempre
  # do mesmo tipo de terminal. O teste n2_build_incremental confere.
  # BS_MSVC_DEPS_JSON (D10, ligado por padrao desde o N2b): as dependencias saem do /sourceDependencies (JSON, sem
  # texto traduzido) e tools/build/msvc_srcdeps.cmake as converte para o ninja. Independe do
  # idioma do MSVC e da pagina de codigo do terminal (teste n2_build_incremental).
  option(BS_MSVC_DEPS_JSON "Dependencias de cabecalho do MSVC por /sourceDependencies" ON)
  if(BS_MSVC_DEPS_JSON AND CMAKE_GENERATOR MATCHES "Ninja")
    foreach(lang C CXX)
      set(CMAKE_${lang}_DEPFILE_FORMAT gcc)
      set(CMAKE_DEPFILE_FLAGS_${lang} "/sourceDependencies <DEP_FILE>.json")
      set(CMAKE_${lang}_COMPILER_LAUNCHER "${CMAKE_COMMAND};-P;${CMAKE_SOURCE_DIR}/tools/build/msvc_srcdeps.cmake;--")
    endforeach()
  elseif(CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CL_SHOWINCLUDES_PREFIX AND
     NOT CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^[ -~]*$")
    message(WARNING "prefixo /showIncludes localizado (\"${CMAKE_CL_SHOWINCLUDES_PREFIX}\"): compile sempre "
                    "do mesmo terminal (pagina de codigo) ou instale o pacote de idioma ingles do Visual Studio")
  endif()
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(tools/extractor)
add_subdirectory(tools/dcmap)
if(BS_BUILD_TOOLS)
  add_subdirectory(translator)
endif()
add_subdirectory(runtime)
# N0: alvos do port nativo (docs/PLANO-PORT-NATIVO.md, docs/N0-RELATORIO.md)
add_subdirectory(native)
add_subdirectory(legacy)
add_subdirectory(transitional)

# Codigo gerado por bs-recomp a partir do executavel do usuario (nao versionado).
set(BS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "Saida do bs-recomp")
if(EXISTS "${BS_GENERATED_DIR}/generated.cmake")
  include("${BS_GENERATED_DIR}/generated.cmake")
  add_library(bs_generated STATIC ${BS_GENERATED_SOURCES})
  target_include_directories(bs_generated PUBLIC "${BS_GENERATED_DIR}")
  target_link_libraries(bs_generated PUBLIC bs_runtime)
  if(BS_MODS)
    target_compile_definitions(bs_generated PUBLIC BS_MODS=1)
  endif()
  if(MSVC)
    # substitui as opcoes herdadas (/W4) para nao gerar o aviso D9025
    set_property(TARGET bs_generated PROPERTY COMPILE_OPTIONS /bigobj /W1 /utf-8 /permissive-)
  endif()
  if(BS_BUILD_TOOLS)
  add_executable(bs-gen-check tools/gen_check/main.cpp)
  target_link_libraries(bs-gen-check PRIVATE bs_generated)
  add_executable(bs-diff tools/diff/main.cpp)
  target_link_libraries(bs-diff PRIVATE bs_generated bs_runtime_cdi)
  endif()
endif()

# Executavel nativo e seu nucleo (app/); o executavel so com BS_BUILD_GAME=ON.
add_subdirectory(app)

# Builder (CDI -> BlueStinger.exe na maquina do usuario)
if(BS_BUILD_TOOLS)
  add_subdirectory(builder)
endif()

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  enable_testing()
  add_subdirectory(tests)
  # Regressao do boot contra o trace do Flycast (dados locais em BS_SNAPSHOT_DIR; senao SKIP).
  find_package(Python3 COMPONENTS Interpreter)
  if(TARGET bs-diff AND Python3_FOUND)
    add_test(NAME boot_trace
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tests/diff/boot_regression.py"
                     $<TARGET_FILE:bs-diff> "${CMAKE_SOURCE_DIR}/tools/dev/flycast_oracle.py"
                     "${CMAKE_SOURCE_DIR}/tests/diff/boot_accepted.tsv")
    # raiz do repositorio: BS_DISC_IMAGE/BS_SNAPSHOT_DIR relativos funcionam
    set_tests_properties(boot_trace PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()
