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

#include <fnmatch.h>
#include <string.h>

#include "macro.h"
#include "match.h"
#include "pkgfile.h"

int match_glob(const filterpattern_t *pattern, const char *line, int UNUSED len,
               int flags) {
  return fnmatch(pattern->glob.glob, line, flags);
}

int match_regex(const filterpattern_t *pattern, const char *line, int len,
                int UNUSED flags) {
  const struct pcre_data *re = &pattern->re;

  return pcre_exec(re->re, re->re_extra, line, len, 0, PCRE_NO_UTF16_CHECK,
                   NULL, 0) < 0;
}

void free_regex(filterpattern_t *pattern) {
  pcre_free(pattern->re.re);
  pcre_free_study(pattern->re.re_extra);
}

int match_exact_basename(const filterpattern_t *pattern, const char *line,
                         int len, int flags) {
  const char *ptr = line, *slash = memrchr(line, '/', len - 1);

  if (slash) {
    ptr = slash + 1;
    len -= ptr - line;
  }

  return match_exact(pattern, ptr, len, flags);
}

int match_exact(const filterpattern_t *pattern, const char *line, int len,
                int flags) {
  if (pattern->glob.globlen != len) {
    return -1;
  }

  return flags ? strcasecmp(pattern->glob.glob, line)
               : memcmp(pattern->glob.glob, line, len);
}

/* vim: set ts=2 sw=2 et: */
