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
	const char *match = (const char *)pattern;
	return flags ? strcasecmp(match, line) : strcmp(match, line);
}

