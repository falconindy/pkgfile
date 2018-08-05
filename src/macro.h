#pragma once

#include <stdio.h>
#include <stdlib.h>

#define ALLOC_FAIL(s)                                                    \
  do {                                                                   \
    fprintf(stderr, "alloc failure: could not allocate %zd bytes\n", s); \
  } while (0)
#define MALLOC(p, s, action) \
  do {                       \
    p = malloc(s);           \
    if (p == NULL) {         \
      ALLOC_FAIL(s);         \
      action;                \
    }                        \
  } while (0)
#define CALLOC(p, l, s, action) \
  do {                          \
    p = calloc(l, s);           \
    if (p == NULL) {            \
      ALLOC_FAIL(s);            \
      action;                   \
    }                           \
  } while (0)
#define FREE(p) \
  do {          \
    free(p);    \
    p = NULL;   \
  } while (0)
#define UNUSED __attribute__((unused))

#define MAX_LINE_SIZE ((size_t)(10 * 1024)) /* used by archive_fgets */

#ifndef MIN
#define MIN(a, b)           \
  __extension__({           \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })
#endif

#ifndef MAX
#define MAX(a, b)           \
  __extension__({           \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
  })
#endif

static inline void freep(void *p) { free(*(void **)p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

/* vim: set ts=2 sw=2 et: */
