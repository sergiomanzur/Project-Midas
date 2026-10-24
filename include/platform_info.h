#ifndef PLATFORM_INFO_H
#define PLATFORM_INFO_H

#ifdef TARGET_N64
#define IS_64_BIT 0
#define IS_BIG_ENDIAN 1
#else
#include <stdint.h>
#define IS_64_BIT (UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFU)
#define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#endif

/* PORT_LP64: a 64-bit host build (arm64). Fixes for 64-bit pointers and structure sizes are guarded with this
 * rather than "!TARGET_N64", so the 32-bit PC and Android builds compile exactly the code they always did. */
#if IS_64_BIT && !defined(TARGET_N64)
#define PORT_LP64 1
#endif

#define DOUBLE_SIZE_ON_64_BIT(size) ((size) * (sizeof(void *) / 4))

#endif /* PLATFORM_INFO_H */
