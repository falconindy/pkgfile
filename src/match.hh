#pragma once

#include "pkgfile.hh"

int match_glob(const filterpattern_t *filter, const char *line, int len,
               int flags);
int match_regex(const filterpattern_t *filter, const char *line, int len,
                int flags);
int match_exact(const filterpattern_t *filter, const char *line, int len,
                int case_insensitive);
int match_exact_basename(const filterpattern_t *filter, const char *line,
                         int len, int case_insensitive);
void free_regex(filterpattern_t *pattern);

/* vim: set ts=2 sw=2 et: */
