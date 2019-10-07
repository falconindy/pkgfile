#pragma once

#include <stdlib.h>

#define MAX_LINE_SIZE ((size_t)(10 * 1024)) /* used by archive_fgets */

static inline void freep(void *p) { free(*(void **)p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

/* vim: set ts=2 sw=2 et: */
