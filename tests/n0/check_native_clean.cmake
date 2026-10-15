# N0: as interfaces nativas nao podem depender do Dreamcast. Falha se algum arquivo de
# native/ menciona o contexto do SH-4, o runtime legado, o mapa de hardware ou enderecos
# tipicos do guest. Uso: cmake -DROOT=<repo> -P check_native_clean.cmake
if(NOT ROOT)
  message(FATAL_ERROR "defina ROOT")
endif()
file(GLOB_RECURSE files "${ROOT}/native/*.h" "${ROOT}/native/*.hpp" "${ROOT}/native/*.cpp")
set(forbidden
  "Sh4Context" "sh4_" "cpu/" "runtime/" "dc_regions" "dcmap" "Region::" "generated/"
  "0x8C0" "0x8C1" "0xA05F" "0x005F" "0xA070" "0x0070" "0xA080" "0xA400" "0xA500" "0xFFD8" "0xFFA0")
set(bad "")
foreach(f ${files})
  file(READ "${f}" text)
  foreach(tok ${forbidden})
    string(FIND "${text}" "${tok}" pos)
    if(NOT pos EQUAL -1)
      list(APPEND bad "${f}: '${tok}'")
    endif()
  endforeach()
endforeach()
list(LENGTH files n)
if(n EQUAL 0)
  message(FATAL_ERROR "nenhum arquivo em native/")
endif()
if(bad)
  string(REPLACE ";" "\n  " out "${bad}")
  message(FATAL_ERROR "native/ depende do Dreamcast:\n  ${out}")
endif()
message(STATUS "native/ limpo (${n} arquivos)")
