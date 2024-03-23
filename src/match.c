#include <fnmatch.h>
#include <string.h>

#include "macro.h"
#include "match.h"
#include "pkgfile.h"

int match_glob(const filterpattern_t *pattern, const char *line, int UNUSED len,
               int flags) {
  return fnmatch(pattern->glob.glob, line, flags);
}

int match_regex(const filterpattern_t *pattern, const char *line, int len,
                int UNUSED flags) {
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(pattern->re.re, NULL);
  int result = pcre2_match(pattern->re.re, (PCRE2_SPTR)line, len, 0,
                   PCRE2_NO_UTF_CHECK, match_data, 0);
  pcre2_match_data_free(match_data);
  return result < 0;
}

void free_regex(filterpattern_t *pattern) {
  pcre2_code_free(pattern->re.re);
}

int match_exact_basename(const filterpattern_t *pattern, const char *line,
                         int len, int case_insensitive) {
  const char *ptr = line, *slash = memrchr(line, '/', len - 1);

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
