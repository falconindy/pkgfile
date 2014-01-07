/*
 * Copyright (C) 2014 by Dave Reisner <dreisner@archlinux.org>
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

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "buffer.h"
#include "macro.h"

void buffer_reset(struct buffer_t *buffer, int freedata) {
  if (buffer == NULL) {
    return;
  }

  buffer->size = 0;

  if (freedata) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->capacity = 0;
  }
}

int buffer_resize(struct buffer_t *buffer, size_t newsize) {
  void *newbuf;

  newbuf = realloc(buffer->data, newsize);
  if (newbuf == NULL) {
    return -ENOMEM;
  }

  buffer->data = newbuf;
  buffer->capacity = newsize;

  return 0;
}

int buffer_append(struct buffer_t *buffer, void *newdata, size_t size) {

  if (buffer->size + size > buffer->capacity) {
    size_t newsize = MAX((size_t)(buffer->capacity * 2.5), buffer->size + size);
    int r = buffer_resize(buffer, newsize);
    if (r < 0) {
      return -r;
    }
  }

  memcpy((char *)buffer->data + buffer->size, newdata, size);
  buffer->size += size;

  return 0;
}

/* vim: set ts=2 sw=2 et: */
