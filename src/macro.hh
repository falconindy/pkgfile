#pragma once

#include <stdio.h>
#include <stdlib.h>

#define ALLOC_FAIL(s)                                                    \
  do {                                                                   \
    fprintf(stderr, "alloc failure: could not allocate %zd bytes\n", s); \
  } while (0)
#define MALLOC(p, s, action)      \
  do {                            \
    p = (__typeof__(p))malloc(s); \
    if (p == NULL) {              \
      ALLOC_FAIL(s);              \
      action;                     \
    }                             \
  } while (0)
#define CALLOC(p, l, s, action)      \
  do {                               \
    p = (__typeof__(p))calloc(l, s); \
    if (p == NULL) {                 \
      ALLOC_FAIL(s);                 \
      action;                        \
    }                                \
  } while (0)

#define MAX_LINE_SIZE ((size_t)(10 * 1024)) /* used by archive_fgets */

static inline void freep(void *p) { free(*(void **)p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

/* vim: set ts=2 sw=2 et: */
