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

#include <limits.h>
#include <stdbool.h>

#include <archive.h>
#include <archive_entry.h>

#include <pcre.h>

#include "result.h"

#ifndef BUFSIZ
#define BUFSIZ 8192
#endif

/* allow compilation with pcre < 8.30 */
#ifndef PCRE_STUDY_JIT_COMPILE
#define PCRE_STUDY_JIT_COMPILE 0
#endif

struct archive_read_buffer {
  char *line;
  char *line_offset;
  off_t line_size;

  char *block;
  char *block_offset;
  size_t block_size;

  long ret;
};

typedef enum _filterstyle_t {
  FILTER_EXACT = 0,
  FILTER_GLOB,
  FILTER_REGEX
} filterstyle_t;

typedef union _filterpattern_t {
  struct pcre_data {
    pcre *re;
    pcre_extra *re_extra;
  } re;
  struct glob_data {
    char *glob;
    int globlen;
  } glob;
} filterpattern_t;

struct pkg_t {
  char name[PATH_MAX];
  const char *version;
  int namelen;
};

struct config_t {
  const char *cfgfile;
  filterstyle_t filterby;
  filterpattern_t filter;
  int (*filefunc)(const char *repo, struct pkg_t *pkg, struct archive *a,
                  struct result_t *result, struct archive_read_buffer *buf);
  int (*filterfunc)(const filterpattern_t *filter, const char *line, int len,
                    int flags);
  void (*filterfree)(filterpattern_t *filter);
  int doupdate;
  char *targetrepo;
  bool binaries;
  bool directories;
  bool icase;
  bool quiet;
  bool verbose;
  bool raw;
  char eol;
  int compress;
};

int archive_fgets(struct archive *a, struct archive_read_buffer *b);

/* vim: set ts=2 sw=2 et: */
