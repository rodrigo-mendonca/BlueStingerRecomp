# N0: fechamento e catraca sobre o executavel real (teste n0_fechamento).
#  1. bs-hwscan com a fronteira declarada e a linha de base: tem de passar (rc 0).
#  2. caso negativo: a mesma fronteira sem uma entrada tem de falhar (rc 3).
# Sem game-data/1ST_READ.BIN (bs-extract), imprime SKIP.
foreach(v HWSCAN ROOT OUT)
  if(NOT ${v})
    message(FATAL_ERROR "defina ${v}")
  endif()
endforeach()
set(exe "${ROOT}/game-data/1ST_READ.BIN")
if(NOT EXISTS "${exe}")
  message(STATUS "SKIP: ${exe} ausente (rode bs-extract)")
  return()
endif()
file(SHA256 "${exe}" sha)
file(READ "${ROOT}/config/bluestinger_us.toml" cfg)
string(FIND "${cfg}" "${sha}" known)
if(known EQUAL -1)
  message(STATUS "SKIP: 1ST_READ.BIN nao e a versao suportada")
  return()
endif()
set(cfgfile "${ROOT}/config/bluestinger_us.toml")
set(boundary "${ROOT}/config/fronteiras_us.toml")
set(base "${ROOT}/config/metricas_base_us.toml")

execute_process(COMMAND "${HWSCAN}" "${cfgfile}" "${exe}" "${OUT}/atual" --fronteira "${boundary}" --metricas-base "${base}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(STATUS "${out}${err}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "fechamento/catraca falhou (rc=${rc}); ver ${OUT}/atual/fechamento.tsv")
endif()

# caso negativo: tira a primeira entrada da fronteira
file(READ "${boundary}" text)
string(FIND "${text}" "[[fronteira]]" first)
math(EXPR after "${first} + 13")
string(SUBSTRING "${text}" ${after} -1 rest)
string(FIND "${rest}" "[[fronteira]]" second)
string(SUBSTRING "${text}" 0 ${first} head)
math(EXPR cut "${after} + ${second}")
string(SUBSTRING "${text}" ${cut} -1 tail)
file(WRITE "${OUT}/fronteira_sem_uma.toml" "${head}${tail}")
execute_process(COMMAND "${HWSCAN}" "${cfgfile}" "${exe}" "${OUT}/negativo" --fronteira "${OUT}/fronteira_sem_uma.toml"
                RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2)
if(NOT rc2 EQUAL 3)
  message(FATAL_ERROR "caso negativo: fronteira sem uma entrada deveria violar o fechamento (rc=${rc2})\n${out2}${err2}")
endif()
message(STATUS "caso negativo: violacao detectada (rc=3)")
