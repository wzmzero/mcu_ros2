#include "micro_ros_memory.h"
#include "FreeRTOS.h"
#include <stdint.h>
#include <string.h>
/* Own metadata, no dependency on private heap_4 block layout. */
typedef union { size_t size; uint64_t alignment; } allocation_header;
void *microros_allocate(size_t size, void *state)
{
    (void)state;
    if (!size || size > SIZE_MAX - sizeof(allocation_header)) return NULL;
    allocation_header *header = pvPortMalloc(sizeof(*header) + size);
    if (!header) return NULL;
    header->size = size;
    return header + 1;
}
void microros_deallocate(void *pointer, void *state)
{
    (void)state;
    if (pointer) vPortFree((allocation_header *)pointer - 1);
}
void *microros_reallocate(void *pointer, size_t size, void *state)
{
    if (!pointer) return microros_allocate(size, state);
    if (!size) { microros_deallocate(pointer, state); return NULL; }
    void *next = microros_allocate(size, state);
    if (!next) return NULL; /* Original allocation remains valid. */
    size_t previous = ((allocation_header *)pointer - 1)->size;
    memcpy(next, pointer, size < previous ? size : previous);
    microros_deallocate(pointer, state);
    return next;
}
void *microros_zero_allocate(size_t count, size_t size, void *state)
{
    if (size && count > SIZE_MAX / size) return NULL;
    void *pointer = microros_allocate(count * size, state);
    if (pointer) memset(pointer, 0, count * size);
    return pointer;
}
