#ifndef MICRO_ROS_MEMORY_H
#define MICRO_ROS_MEMORY_H
#include <stddef.h>
void *microros_allocate(size_t size, void *state);
void microros_deallocate(void *pointer, void *state);
void *microros_reallocate(void *pointer, size_t size, void *state);
void *microros_zero_allocate(size_t count, size_t size, void *state);
#endif
