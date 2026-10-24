# D6 (docs/N2-PLANO.md): build incremental confiavel. Um objeto sem dependencias de
# cabecalho registradas nao e recompilado quando um .h muda (no N1, main_sdl.cpp.obj velho
# deu estouro de pilha so no build local). Tres partes:
#   1. a arvore de build do projeto: nenhum objeto C/C++ do projeto (fora do SDL3 e de .asm) pode
#      estar sem dependencias no registro do ninja; recursos .rc nao usam o compilador C/C++ e o
#      projeto rastreia o icone no configure_file que gera o .rc (app/CMakeLists.txt);
#   2. um projeto minimo: o caminho textual do /showIncludes e observado (pode nao funcionar em
#      terminais localizados, razao para o caminho JSON) e o caminho JSON tem de recompilar depois
#      de mudar o cabecalho;
#   3. caso negativo: o projeto com o prefixo /showIncludes trocado tem de ficar sem dependencias.
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
  if(NOT o MATCHES "^_deps/" AND NOT o MATCHES "\\.asm\\.obj$" AND NOT o MATCHES "\\.rc\\.res$")
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

# 2 a 4. projeto minimo, em tres modos:
#   msvc     - dependencias pelo /showIncludes (padrao do CMake; o prefixo e traduzido);
#   quebrado - o mesmo com o prefixo trocado (caso negativo: tem de ficar sem dependencias);
#   json     - BS_MSVC_DEPS_JSON: /sourceDependencies + tools/build/msvc_srcdeps.cmake
#              (independe do idioma e da pagina de codigo).
function(mini name mode out_rebuilds out_deps)
  set(src "${WORK}/${name}")
  file(REMOVE_RECURSE "${src}")
  file(MAKE_DIRECTORY "${src}")
  set(extra "")
  set(btype "")
  if(mode STREQUAL "json")
    # Debug: /Zi com /Fd<pasta>\ /FS (barra invertida no fim de um argumento: achado do N2b)
    set(btype "-DCMAKE_BUILD_TYPE=Debug")
    string(CONCAT extra "set(CMAKE_CXX_DEPFILE_FORMAT gcc)\n"
                        "set(CMAKE_DEPFILE_FLAGS_CXX \"/sourceDependencies <DEP_FILE>.json\")\n"
                        "set(CMAKE_CXX_COMPILER_LAUNCHER \"\${CMAKE_COMMAND};-P;${SRCDEPS};--\")\n")
  endif()
  file(WRITE "${src}/CMakeLists.txt"
       "cmake_minimum_required(VERSION 3.25)\nproject(inc LANGUAGES CXX)\n${extra}add_executable(inc a.cpp main.cpp)\n")
  # executavel (nao biblioteca): no Debug o CMake passa /Fd<pasta>\ com barra no fim, o caso que
  # quebrou o lancador no N2b (na biblioteca o /Fd recebe um nome de arquivo)
  file(WRITE "${src}/main.cpp" "#include \"a.h\"\nint usa();\nint main() { return usa() + valor(); }\n")
  file(WRITE "${src}/a.h" "inline int valor() { return 1; }\n")
  file(WRITE "${src}/a.cpp" "#include \"a.h\"\nint usa() { return valor(); }\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${src}" -B "${src}/b" -G Ninja "-DCMAKE_MAKE_PROGRAM=${NINJA}"
                          "-DCMAKE_CXX_COMPILER=${CXX}" ${btype}
                  OUTPUT_QUIET ERROR_VARIABLE err RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "configuracao do projeto minimo (${name}) falhou:\n${err}")
  endif()
  if(mode STREQUAL "quebrado")  # prefixo que nunca aparece na saida do compilador
    file(READ "${src}/b/CMakeFiles/rules.ninja" rules)
    string(REGEX REPLACE "msvc_deps_prefix = [^\n]*" "msvc_deps_prefix = PREFIXO_ERRADO:" rules "${rules}")
    file(WRITE "${src}/b/CMakeFiles/rules.ninja" "${rules}")
  endif()
  execute_process(COMMAND "${NINJA}" -C "${src}/b" OUTPUT_VARIABLE bout ERROR_VARIABLE berr RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "build do projeto minimo (${name}) falhou:\n${bout}${berr}")
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

mini(ok msvc rebuilds missingOk)
if(missingOk EQUAL 0)
  if(NOT rebuilds)
    message(FATAL_ERROR "o caminho /showIncludes registrou dependencias, mas nao recompila depois de mudar um cabecalho")
  endif()
else()
  message(STATUS "controle /showIncludes textual indisponivel neste terminal (${missingOk} objetos sem dependencias); "
                 "o caminho JSON obrigatorio e verificado abaixo")
endif()
mini(negativo quebrado rebuildsNeg missingNeg)
if(rebuildsNeg OR missingNeg EQUAL 0)
  message(FATAL_ERROR "caso negativo nao detectado: com prefixo errado o objeto deveria ficar sem dependencias")
endif()
if(SRCDEPS)
  mini(json json rebuildsJson missingJson)
  if(NOT rebuildsJson OR NOT missingJson EQUAL 0)
    message(FATAL_ERROR "modo BS_MSVC_DEPS_JSON: dependencias nao registradas (objetos sem dependencias: ${missingJson})")
  endif()
  # caminho com espaco, "#" e "$" (escape do depfile, revisao Codex 2026-10-15): alem de pedir o
  # plano ao ninja, faz o segundo build de verdade e confere que o objeto foi recompilado
  # ("$$" no nome: sem o escape para o ninja, o depfile o transforma em "$" e o caminho gravado
  # deixa de ser o do cabecalho; o teste confere o caminho exato registrado)
  set(special "json esp #1 $$x")
  mini("${special}" json rebuildsSp missingSp)
  set(sb "${WORK}/${special}/b")
  execute_process(COMMAND "${NINJA}" -C "${sb}" -t deps OUTPUT_VARIABLE spDeps)
  string(TOLOWER "${spDeps}" spDeps)
  string(TOLOWER "${WORK}/${special}/a.h" spHeader)
  string(FIND "${spDeps}" "${spHeader}" spHit)
  if(spHit EQUAL -1)
    message(FATAL_ERROR "depfile com escape errado: o ninja nao registrou ${spHeader}\n${spDeps}")
  endif()
  file(TIMESTAMP "${sb}/CMakeFiles/inc.dir/a.cpp.obj" before "%s")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
  file(WRITE "${WORK}/${special}/a.h" "inline int valor() { return 3; }\n")
  execute_process(COMMAND "${NINJA}" -C "${sb}" OUTPUT_VARIABLE sout ERROR_VARIABLE serr RESULT_VARIABLE rc)
  file(TIMESTAMP "${sb}/CMakeFiles/inc.dir/a.cpp.obj" after "%s")
  if(NOT rc EQUAL 0 OR NOT rebuildsSp OR NOT missingSp EQUAL 0 OR NOT after GREATER before)
    message(FATAL_ERROR "modo BS_MSVC_DEPS_JSON em caminho com espaco/#/$: rc=${rc}, sem dependencias ${missingSp}, "
                        "objeto ${before} -> ${after}\n${sout}${serr}")
  endif()
  # negativo: o compilador falha -> build com codigo nao zero e sem objeto
  file(WRITE "${WORK}/json/a.cpp" "#include \"a.h\"\nint usa() { return valor( }\n")
  file(REMOVE "${WORK}/json/b/CMakeFiles/inc.dir/a.cpp.obj")
  execute_process(COMMAND "${NINJA}" -C "${WORK}/json/b" OUTPUT_VARIABLE fout ERROR_VARIABLE ferr RESULT_VARIABLE rcf)
  if(rcf EQUAL 0 OR EXISTS "${WORK}/json/b/CMakeFiles/inc.dir/a.cpp.obj" OR NOT fout MATCHES "error C")
    message(FATAL_ERROR "negativo: compilador com erro deveria falhar o build sem objeto e com a mensagem (rc=${rcf})")
  endif()
  file(WRITE "${WORK}/json/a.cpp" "#include \"a.h\"\nint usa() { return valor(); }\n")
  # 5. o modo JSON nao depende da pagina de codigo: recompila num console separado com a pagina
  #    850 (a do Git Bash, que perdia as dependencias no N1/N2a), sem mexer no console atual
  set(jb "${WORK}/json/b")
  file(REMOVE "${jb}/.ninja_deps" "${jb}/CMakeFiles/inc.dir/a.cpp.obj")
  file(TO_NATIVE_PATH "${NINJA}" ninjaNative)
  file(TO_NATIVE_PATH "${jb}" jbNative)
  execute_process(COMMAND cmd /c start "" /wait /min cmd /c "chcp 850 >nul & \"${ninjaNative}\" -C \"${jbNative}\""
                  RESULT_VARIABLE rc)
  objects_without_deps("${jb}" m850)
  list(LENGTH m850 n850)
  if(NOT rc EQUAL 0 OR NOT n850 EQUAL 0)
    message(FATAL_ERROR "modo BS_MSVC_DEPS_JSON perdeu dependencias com a pagina de codigo 850 (rc=${rc}, sem dependencias: ${n850})")
  endif()
endif()
message(STATUS "build incremental ok: projeto sem objetos sem dependencias; cabecalho recompila com "
               "/showIncludes e com /sourceDependencies (BS_MSVC_DEPS_JSON); negativo detectado")
