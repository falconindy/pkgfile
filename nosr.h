/*
 * Copyright (C) 2011 by Dave Reisner <dreisner@archlinux.org>
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

#ifndef _NOSR_H
#define _NOSR_H

#include <archive.h>
#include <archive_entry.h>

#include <pcre.h>

#include "result.h"

#define DBPATH  "/var/cache/nosr"

struct archive_read_buffer {
	char *line;
	char *line_offset;
	size_t line_size;
	size_t max_line_size;

	char *block;
	char *block_offset;
	size_t block_size;

	int ret;
};

struct pcre_data {
	pcre *re;
	pcre_extra *re_extra;
};

typedef enum _filterstyle_t {
	FILTER_EXACT = 0,
	FILTER_GLOB,
	FILTER_REGEX
} filterstyle_t;

typedef union _filterpattern_t {
	struct pcre_data re;
	char *glob;
} filterpattern_t;

struct pkg_t {
	char *name;
	char *version;
};

struct config_t {
	filterstyle_t filterby;
	filterpattern_t filter;
	int (*filefunc)(const char *repo, struct pkg_t *pkg, struct archive* a,
			struct result_t *result);
	int (*filterfunc)(filterpattern_t *filter, const char *line, size_t len,
			int flags);
	void (*filterfree)(filterpattern_t *filter);
	char *targetrepo;
	int binaries;
	int icase;
	int doupdate;
	int verbose;
};

#endif /* _NOSR_H */

/* vim: set ts=2 sw=2 noet: */
