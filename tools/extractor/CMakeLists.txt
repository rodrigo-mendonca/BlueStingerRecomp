add_library(bs_disc STATIC
  disc/disc_image.cpp
  disc/cdi_image.cpp
  disc/iso9660.cpp
  disc/ip_bin.cpp
  disc/scramble.cpp
  util/hash.cpp
  extract.cpp
)
target_include_directories(bs_disc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_definitions(bs_disc PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(bs-extract main.cpp)
target_link_libraries(bs-extract PRIVATE bs_disc)
