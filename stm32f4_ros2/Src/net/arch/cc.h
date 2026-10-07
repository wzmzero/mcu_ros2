#ifndef APP_LWIP_CC_H
#define APP_LWIP_CC_H
#include <stdint.h>
#include <stdlib.h>
#include "board.h"
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); board_panic(); } while (0)
#define LWIP_RAND() ((uint32_t)rand())
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x
#endif
