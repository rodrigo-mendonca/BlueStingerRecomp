#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* Blue Stinger Native PC: gerado a mao no lugar do configure/CMake do libogg (config_types.h.in da 1.3.6), com os
   tipos de <stdint.h> (Windows x64 e Linux x86_64). Unico arquivo daqui que nao veio do tarball oficial. */
#define INCLUDE_INTTYPES_H 0
#define INCLUDE_STDINT_H 1
#define INCLUDE_SYS_TYPES_H 0

#include <stdint.h>

typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#endif
