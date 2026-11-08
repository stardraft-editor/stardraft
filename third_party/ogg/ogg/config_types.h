#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

/* libogg's config_types.h (CMakeLists.txt builds libogg from its sources with this file in place of the one that its
   configure step makes from include/ogg/config_types.h.in). Every compiler that StarDraft builds with has stdint.h. */

#include <stdint.h>

typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;

#endif
