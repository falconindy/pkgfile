#include <string.h>

#include <algorithm>

#include "macro.hh"
#include "result.hh"

int result_add(struct result_t *result, char *prefix, char *entry,
               int prefixlen) {
  if (!result || !prefix) {
    return 1;
  }

  if (prefixlen > result->max_prefixlen) {
    result->max_prefixlen = prefixlen;
  }

  result->lines.emplace_back(prefix, entry ? entry : "");

  return 0;
}

static int linecmp(const struct line_t &line1, const struct line_t &line2) {
  if (line1.prefix == line2.prefix && !line1.prefix.empty()) {
    return line1.entry < line2.entry;
  }

  return line1.prefix < line2.prefix;
}

static void result_print_two_columns(struct result_t *result, int prefixlen,
                                     char eol) {
  for (const auto &line : result->lines) {
    printf("%-*s\t%s%c", prefixlen, line.prefix.c_str(), line.entry.c_str(),
           eol);
  }
}

static void result_print_one_column(struct result_t *result, char eol) {
  for (const auto &line : result->lines) {
    printf("%s%c", line.prefix.c_str(), eol);
  }
}

size_t result_print(struct result_t *result, int prefixlen, char eol) {
  std::sort(result->lines.begin(), result->lines.end(), linecmp);

  /* It's expected that results are homogenous. */
  if (!result->lines[0].entry.empty()) {
    result_print_two_columns(result, prefixlen, eol);
  } else {
    result_print_one_column(result, eol);
  }

  return result->lines.size();
}

size_t results_get_prefixlen(const std::vector<result_t> &results) {
  return std::max_element(results.begin(), results.end(),
                          [](const result_t &a, const result_t &b) {
                            return a.max_prefixlen < b.max_prefixlen;
                          })
      ->max_prefixlen;
}

/* vim: set ts=2 sw=2 et: */
