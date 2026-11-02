// Zero-valued heap telemetry is deliberate: these are not ESP32 measurements.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_32BIT 4U
extern unsigned hostHeapAllocations;
inline void* heap_caps_malloc(size_t bytes,unsigned) {
  ++hostHeapAllocations;
  return malloc(bytes);
}
inline size_t heap_caps_get_largest_free_block(unsigned) { return 0U; }
inline size_t heap_caps_get_free_size(unsigned) { return 0U; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return 0U; }
