#define _GNU_SOURCE
#include <fnmatch.h>
#include <string.h>

#include "match.h"
#include "nosr.h"

int match_glob(void *pattern, const char *line, int flags) {
	const char *glob = (const char *)pattern;
	return fnmatch(glob, line, flags);
}

int match_regex(void *pattern, const char *line, int flags) {
	struct pcre_data *re = (struct pcre_data *)pattern;
	return !(pcre_exec(re->re, re->re_extra, line, strlen(line),
			0, flags, NULL, 0) >= 0);
}

int match_exact(void *pattern, const char *line, int flags) {
	const char *ptr, *match = (const char *)pattern;

	/* if the search string contains a /, don't just search on basenames. since
	 * our files DB doesn't contain leading slashes (for good reason), advance
	 * the pointer on the line to compare against */
	if(match[0] == '/') {
		ptr = line;
		match++;
	} else {
		ptr = strrchr(line, '/');
		if(ptr) {
			ptr++;
		} else {
			/* invalid? we should never hit this */
			return 1;
		}
	}

	return flags ? strcasecmp(match, ptr) : strcmp(match, ptr);
}

/* vim: set ts=2 sw=2 noet: */
