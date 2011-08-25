#ifndef _NOSR_MATCH_H
#define _NOSR_MATCH_H

#include "nosr.h"

int match_glob(filterpattern_t *filter, const char *line, int flags);
int match_regex(filterpattern_t *filter, const char *line, int flags);
void free_regex(filterpattern_t *pattern);
int match_exact(filterpattern_t *filter, const char *line, int flags);

#endif /* _NOSR_MATCH_H */

/* vim: set ts=2 sw=2 noet: */
