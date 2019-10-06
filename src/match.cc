#include <fnmatch.h>
#include <string.h>

#include "macro.hh"
#include "match.hh"
#include "pkgfile.hh"

int match_glob(const filterpattern_t *pattern, const char *line, int UNUSED len,
               int flags) {
  return fnmatch(pattern->glob.glob, line, flags);
}

int match_regex(const filterpattern_t *pattern, const char *line, int len,
                int UNUSED flags) {
  return pcre_exec(pattern->re.re, pattern->re.re_extra, line, len, 0,
                   PCRE_NO_UTF16_CHECK, NULL, 0) < 0;
}

void free_regex(filterpattern_t *pattern) {
  pcre_free(pattern->re.re);
  pcre_free_study(pattern->re.re_extra);
}

int match_exact_basename(const filterpattern_t *pattern, const char *line,
                         int len, int case_insensitive) {
  const char *ptr = line, *slash = (char *)memrchr(line, '/', len - 1);

  if (slash) {
    ptr = slash + 1;
    len -= ptr - line;
  }

  return match_exact(pattern, ptr, len, case_insensitive);
}

int match_exact(const filterpattern_t *pattern, const char *line, int len,
                int case_insensitive) {
  if (pattern->glob.globlen != len) {
    return -1;
  }

  return case_insensitive ? strcasecmp(pattern->glob.glob, line)
                          : memcmp(pattern->glob.glob, line, len);
}

/* vim: set ts=2 sw=2 et: */
