/*
 * Copyright (C) 2011-2012 by Dave Reisner <dreisner@archlinux.org>
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

#include <stdbool.h>

#include <archive.h>
#include <archive_entry.h>

#include <pcre.h>

#include "result.h"

#define CACHEPATH  "/var/cache/pkgfile"
#define DBPATH     "/var/lib/pacman"

#ifndef BUFSIZ
# define BUFSIZ 8192
#endif

/* allow compilation with pcre < 8.30 */
#ifndef PCRE_STUDY_JIT_COMPILE
# define PCRE_STUDY_JIT_COMPILE 0
#endif

/* allow compilation with libarchive < 3.0 */
#if ARCHIVE_VERSION_NUMBER < 3000000
# define archive_read_free(x)  archive_read_finish(x)
# define archive_write_free(x) archive_write_finish(x)

# define archive_write_add_filter_none(x)   archive_write_set_compression_none(x)
# define archive_write_add_filter_gzip(x)   archive_write_set_compression_gzip(x)
# define archive_write_add_filter_bzip2(x)  archive_write_set_compression_bzip2(x)
# define archive_write_add_filter_lzma(x)   archive_write_set_compression_lzma(x)
# define archive_write_add_filter_xz(x)     archive_write_set_compression_xz(x)
#endif

struct archive_read_buffer {
	char *line;
	char *line_offset;
	size_t line_size;
	size_t max_line_size;
	size_t real_line_size;

	char *block;
	char *block_offset;
	size_t block_size;

	int ret;
};

typedef enum _filterstyle_t {
	FILTER_EXACT = 0,
	FILTER_GLOB,
	FILTER_REGEX
} filterstyle_t;

typedef enum _compresstype_t {
	COMPRESS_NONE = 0,
	COMPRESS_GZIP,
	COMPRESS_BZIP2,
	COMPRESS_LZMA,
	COMPRESS_XZ,
	COMPRESS_INVALID
} compresstype_t;

typedef union _filterpattern_t {
	struct pcre_data {
		pcre *re;
		pcre_extra *re_extra;
	} re;
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
	compresstype_t compress;
};

/* vim: set ts=2 sw=2 noet: */
