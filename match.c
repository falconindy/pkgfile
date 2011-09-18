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

#define _GNU_SOURCE
#include <fnmatch.h>
#include <string.h>

#include "match.h"
#include "nosr.h"
#include "util.h"

int match_glob(filterpattern_t *pattern, const char *line, size_t UNUSED len,
		int flags)
{
	const char *glob = pattern->glob;
	
	if(glob[0] == '/') {
		glob++;
	}

	return fnmatch(glob, line, flags);
}

int match_regex(filterpattern_t *pattern, const char *line, size_t len,
		int UNUSED flags)
{
	struct pcre_data *re = &pattern->re;

	if(len == (size_t)-1) {
		len = strlen(line);
	}

	return !(pcre_exec(re->re, re->re_extra, line, len, 0, 0, NULL, 0) >= 0);
}

void free_regex(filterpattern_t *pattern) {
	pcre_free(pattern->re.re);
	pcre_free(pattern->re.re_extra);
}

int match_exact(filterpattern_t *pattern, const char *line, size_t len, int flags)
{
	const char *ptr = line, *match = pattern->glob;

	/* if the search string contains a /, don't just search on basenames. since
	 * our files DB doesn't contain leading slashes (for good reason), advance
	 * the pointer on the line to compare against */
	if(match[0] == '/') {
		match++;
	} else {
		const char *slash =
			len == (size_t)-1 ? strrchr(line, '/') : memrchr(line, '/', len - 1);
		if(slash) {
			ptr = slash + 1;
		}
	}

	return flags ? strcasecmp(match, ptr) : strcmp(match, ptr);
}

/* vim: set ts=2 sw=2 noet: */
