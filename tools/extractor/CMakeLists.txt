# N8.2: dados do jogo para o produto (bs_disc_data: RuntimeDisc e a conferencia dos dados de execucao, IP.BIN,
# descrambler, ISO9660 sobre a interface generica de disco, hash) e leitura de imagens para as ferramentas (bs_disc:
# leitor de CDI e o extrator). O executavel do jogo liga so bs_disc_data (catraca n8_produto_papeis).
add_library(bs_disc_data STATIC
  disc/disc_image.cpp
  disc/iso9660.cpp
  disc/ip_bin.cpp
  disc/scramble.cpp
  util/hash.cpp
  runtime_data.cpp
)
target_include_directories(bs_disc_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_definitions(bs_disc_data PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

add_library(bs_disc STATIC
  disc/cdi_image.cpp
  extract.cpp
)
target_link_libraries(bs_disc PUBLIC bs_disc_data)

add_executable(bs-extract main.cpp)
target_link_libraries(bs-extract PRIVATE bs_disc)
