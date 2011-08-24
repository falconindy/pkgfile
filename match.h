#ifndef _NOSR_MATCH_H
#define _NOSR_MATCH_H

int match_glob(void *glob, const char *line, int flags);
int match_regex(void *regex, const char *line, int flags);
int match_exact(void *match, const char *line, int flags);

#endif /* _NOSR_MATCH_H */

/* vim: set ts=2 sw=2 noet: */
