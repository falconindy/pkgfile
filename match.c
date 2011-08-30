#define _GNU_SOURCE
#include <fnmatch.h>
#include <string.h>

#include "match.h"
#include "nosr.h"
#include "util.h"

int match_glob(filterpattern_t *pattern, const char *line, int flags) {
	const char *glob = pattern->glob;
	return fnmatch(glob, line, flags);
}

int match_regex(filterpattern_t *pattern, const char *line, int UNUSED flags) {
	struct pcre_data *re = &pattern->re;
	return !(pcre_exec(re->re, re->re_extra, line, strlen(line),
			0, 0, NULL, 0) >= 0);
}

void free_regex(filterpattern_t *pattern) {
	struct pcre_data *re = &pattern->re;
	pcre_free(re->re);
	pcre_free(re->re_extra);
}

int match_exact(filterpattern_t *pattern, const char *line, int flags) {
	const char *ptr = line, *match = pattern->glob;

	/* if the search string contains a /, don't just search on basenames. since
	 * our files DB doesn't contain leading slashes (for good reason), advance
	 * the pointer on the line to compare against */
	if(match[0] == '/') {
		match++;
	} else {
		const char *slash = strrchr(line, '/');
		if(slash) {
			ptr = slash + 1;
		}
	}

	return flags ? strcasecmp(match, ptr) : strcmp(match, ptr);
}

/* vim: set ts=2 sw=2 noet: */
