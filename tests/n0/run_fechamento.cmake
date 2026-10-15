# N0: fechamento e catraca sobre o executavel real (teste n0_fechamento).
#  1. bs-hwscan com a fronteira declarada e a linha de base: tem de passar (rc 0).
#  2. caso negativo: a mesma fronteira sem uma entrada tem de falhar (rc 3).
#  3. caso negativo (N2a, A8): um modulo do runtime que volte a medir tempo pelo contador de
#     ciclos sobe tempo.consumidores.ciclos e a catraca falha (rc 4).
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
                        --runtime "${ROOT}/runtime"
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

# casos negativos do N2a: copia do runtime com uma leitura funcional de c->cycles num modulo
# (Maple) e nos arquivos que tem leituras permitidas (poll central e entrada de compatibilidade):
# so as expressoes conhecidas sao isentas
foreach(target maple/maple.cpp cpu/sh4_runtime.cpp cpu/time_hooks.h)
  string(MAKE_C_IDENTIFIER "${target}" tag)
  file(REMOVE_RECURSE "${OUT}/runtime_neg")
  file(COPY "${ROOT}/runtime/" DESTINATION "${OUT}/runtime_neg")
  file(APPEND "${OUT}/runtime_neg/${target}" "\nnamespace bs { inline uint64_t n2_neg(Sh4Context* c) { return c->cycles + 1; } }\n")
  execute_process(COMMAND "${HWSCAN}" "${cfgfile}" "${exe}" "${OUT}/negativo_ciclos_${tag}" --fronteira "${boundary}"
                          --metricas-base "${base}" --runtime "${OUT}/runtime_neg"
                  RESULT_VARIABLE rc3 OUTPUT_VARIABLE out3 ERROR_VARIABLE err3)
  if(NOT rc3 EQUAL 4 OR NOT out3 MATCHES "tempo.consumidores.ciclos")
    message(FATAL_ERROR "caso negativo: leitura de ciclos em ${target} deveria subir a catraca (rc=${rc3})\n${out3}${err3}")
  endif()
  message(STATUS "caso negativo: leitura de ciclos em ${target} detectada (rc=4)")
endforeach()
file(REMOVE_RECURSE "${OUT}/runtime_neg")
