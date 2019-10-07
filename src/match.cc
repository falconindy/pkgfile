#include <fnmatch.h>
#include <string.h>

#include "match.hh"
#include "pkgfile.hh"

int match_glob(const filterpattern_t* pattern, std::string_view line,
               int flags) {
  // we know this is null terminated, so line.data() is safe.
  return fnmatch(pattern->glob.glob, line.data(), flags);
}

int match_regex(const filterpattern_t* pattern, std::string_view line, int) {
  return pcre_exec(pattern->re.re, pattern->re.re_extra, line.data(),
                   line.size(), 0, PCRE_NO_UTF16_CHECK, nullptr, 0) < 0;
}

void free_regex(filterpattern_t* pattern) {
  pcre_free(pattern->re.re);
  pcre_free_study(pattern->re.re_extra);
}

int match_exact_basename(const filterpattern_t* pattern, std::string_view line,
                         int case_insensitive) {
  auto slash = line.rfind('/');

  if (slash != std::string_view::npos) {
    line.remove_prefix(slash + 1);
  }

  return match_exact(pattern, line, case_insensitive);
}

int match_exact(const filterpattern_t* pattern, std::string_view line,
                int case_insensitive) {
  if (pattern->glob.globlen != line.size()) {
    return 1;
  }

  return case_insensitive
             ? strncasecmp(pattern->glob.glob, line.data(), line.size())
             : memcmp(pattern->glob.glob, line.data(), line.size());
}

// vim: set ts=2 sw=2 et:
