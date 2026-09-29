#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
inline size_t heap_caps_get_free_size(int c) { return c == MALLOC_CAP_SPIRAM ? 3900000 : 118000; }
inline size_t heap_caps_get_minimum_free_size(int) { return 96000; }
inline size_t heap_caps_get_largest_free_block(int) { return 60000; }
