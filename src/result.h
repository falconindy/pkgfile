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

#include <sys/types.h>

struct line_t {
  char *prefix;
  char *entry;
};

struct result_t {
  size_t size;
  size_t capacity;
  char *name;
  struct line_t **lines;
  int max_prefixlen;
};

struct result_t *result_new(char *name, size_t initial_size);
int result_add(struct result_t *result, char *repo, char *entry, int prefixlen);
void result_free(struct result_t *result);
size_t result_print(struct result_t *result, int prefixlen, char eol);
int result_cmp(const void *r1, const void *r2);
int results_get_prefixlen(struct result_t **results, int count);

/* vim: set ts=2 sw=2 et: */
