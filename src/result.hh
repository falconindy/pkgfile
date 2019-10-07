#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

struct line_t {
  line_t(std::string prefix, std::string entry)
      : prefix(std::move(prefix)), entry(std::move(entry)) {}

  line_t(line_t&&) = default;
  line_t& operator=(line_t&&) = default;

  std::string prefix;
  std::string entry;
};

struct result_t {
  result_t(const std::string& name) : name(name) {}

  result_t(result_t&&) = default;
  result_t& operator=(result_t&&) = default;

  std::string name;
  std::vector<line_t> lines;
  int max_prefixlen = 0;
};

int result_add(struct result_t* result, std::string prefix, std::string entry,
               int prefixlen);
size_t result_print(struct result_t* result, int prefixlen, char eol);
size_t results_get_prefixlen(const std::vector<result_t>& results);

// vim: set ts=2 sw=2 et:
