# D6 (docs/N2-PLANO.md): build incremental confiavel. Um objeto sem dependencias de
# cabecalho registradas nao e recompilado quando um .h muda (no N1, main_sdl.cpp.obj velho
# deu estouro de pilha so no build local). Tres partes:
#   1. a arvore de build do projeto: nenhum objeto do projeto (fora do SDL3 e de .asm) pode
#      estar sem dependencias no registro do ninja;
#   2. um projeto minimo: depois de mudar o cabecalho, o ninja tem de recompilar o objeto;
#   3. caso negativo: o mesmo projeto com o prefixo /showIncludes trocado tem de ser detectado.
# Uso: cmake -DNINJA=<ninja> -DBIN=<build> -DCXX=<compilador> -DWORK=<pasta> -P check_build_incremental.cmake
foreach(v NINJA BIN CXX WORK)
  if(NOT ${v})
    message(FATAL_ERROR "defina ${v}")
  endif()
endforeach()
if(NOT EXISTS "${BIN}/.ninja_deps")
  message("SKIP: ${BIN} nao e uma arvore do Ninja")
  return()
endif()

# objetos sem dependencias (linhas "<obj>: #deps 0, ...") de `ninja -t deps`
function(objects_without_deps dir out)
  execute_process(COMMAND "${NINJA}" -C "${dir}" -t deps OUTPUT_VARIABLE txt RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ninja -t deps falhou em ${dir}")
  endif()
  string(REGEX MATCHALL "[^\n]+: #deps 0," lines "${txt}")
  set(objs "")
  foreach(l ${lines})
    string(REGEX REPLACE ": #deps 0,$" "" o "${l}")
    list(APPEND objs "${o}")
  endforeach()
  set(${out} "${objs}" PARENT_SCOPE)
endfunction()

# 1. arvore do projeto
objects_without_deps("${BIN}" missing)
set(bad "")
foreach(o ${missing})
  if(NOT o MATCHES "^_deps/" AND NOT o MATCHES "\\.asm\\.obj$")
    list(APPEND bad "${o}")
  endif()
endforeach()
if(bad)
  list(LENGTH bad n)
  list(SUBLIST bad 0 10 shown)
  string(REPLACE ";" "\n  " shown "${shown}")
  message(FATAL_ERROR "${n} objetos do projeto sem dependencias de cabecalho (build de outro terminal? "
                      "faca um build limpo do mesmo terminal):\n  ${shown}")
endif()

# 2 e 3. projeto minimo
function(mini name breakPrefix out_rebuilds out_deps)
  set(src "${WORK}/${name}")
  file(REMOVE_RECURSE "${src}")
  file(MAKE_DIRECTORY "${src}")
  file(WRITE "${src}/CMakeLists.txt"
       "cmake_minimum_required(VERSION 3.25)\nproject(inc LANGUAGES CXX)\nadd_library(inc STATIC a.cpp)\n")
  file(WRITE "${src}/a.h" "inline int valor() { return 1; }\n")
  file(WRITE "${src}/a.cpp" "#include \"a.h\"\nint usa() { return valor(); }\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${src}" -B "${src}/b" -G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA}"
                          "-DCMAKE_CXX_COMPILER=${CXX}"
                  OUTPUT_QUIET ERROR_VARIABLE err RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "configuracao do projeto minimo falhou:\n${err}")
  endif()
  if(breakPrefix)  # caso negativo: prefixo que nunca aparece na saida do compilador
    file(READ "${src}/b/CMakeFiles/rules.ninja" rules)
    string(REGEX REPLACE "msvc_deps_prefix = [^\n]*" "msvc_deps_prefix = PREFIXO_ERRADO:" rules "${rules}")
    file(WRITE "${src}/b/CMakeFiles/rules.ninja" "${rules}")
  endif()
  execute_process(COMMAND "${NINJA}" -C "${src}/b" OUTPUT_QUIET RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "build do projeto minimo falhou")
  endif()
  objects_without_deps("${src}/b" m)
  list(LENGTH m nm)
  # muda o cabecalho (conteudo e data) e pergunta ao ninja o que recompilaria
  file(WRITE "${src}/a.h" "inline int valor() { return 2; }\n")
  file(TOUCH "${src}/a.h")
  execute_process(COMMAND "${NINJA}" -C "${src}/b" -n OUTPUT_VARIABLE plan)
  string(FIND "${plan}" "a.cpp" hit)
  if(hit EQUAL -1)
    set(${out_rebuilds} FALSE PARENT_SCOPE)
  else()
    set(${out_rebuilds} TRUE PARENT_SCOPE)
  endif()
  set(${out_deps} ${nm} PARENT_SCOPE)
endfunction()

mini(ok FALSE rebuilds missingOk)
if(NOT rebuilds OR NOT missingOk EQUAL 0)
  message(FATAL_ERROR "build incremental nao recompila depois de mudar um cabecalho (objetos sem dependencias: "
                      "${missingOk}). O prefixo /showIncludes nao confere com a saida do compilador neste terminal.")
endif()
mini(negativo TRUE rebuildsNeg missingNeg)
if(rebuildsNeg OR missingNeg EQUAL 0)
  message(FATAL_ERROR "caso negativo nao detectado: com prefixo errado o objeto deveria ficar sem dependencias")
endif()
message(STATUS "build incremental ok: projeto sem objetos sem dependencias; cabecalho recompila; negativo detectado")
