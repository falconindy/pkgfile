/*
 * Copyright (C) 2011-2013 by Dave Reisner <dreisner@archlinux.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

#define ALLOC_FAIL(s) do { fprintf(stderr, "alloc failure: could not allocate %zd bytes\n", s); } while(0)
#define MALLOC(p, s, action) do { p = malloc(s); if(p == NULL) { ALLOC_FAIL(s); action; } } while(0)
#define CALLOC(p, l, s, action) do { p = calloc(l, s); if(p == NULL) { ALLOC_FAIL(s); action; } } while(0)
#define FREE(p) do { free(p); p = NULL; } while(0)
#define UNUSED __attribute__((unused))

#define MAX_LINE_SIZE ((size_t)(10 * 1024)) /* used by archive_fgets */

#ifndef MIN
#define MIN(a,b)                                \
        __extension__ ({                        \
                        __typeof__(a) _a = (a);     \
                        __typeof__(b) _b = (b);     \
                        _a < _b ? _a : _b;      \
                })
#endif

#ifndef MAX
#define MAX(a,b)                                \
        __extension__ ({                        \
                        __typeof__(a) _a = (a);     \
                        __typeof__(b) _b = (b);     \
                        _a > _b ? _a : _b;      \
                })
#endif

/* vim: set ts=2 sw=2 noet: */
