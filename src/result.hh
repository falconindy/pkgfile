#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

struct line_t {
  line_t(std::string prefix, std::string entry)
      : prefix(std::move(prefix)), entry(std::move(entry)) {}

  std::string prefix;
  std::string entry;
};

struct result_t {
  result_t(const std::string &name) : name(name) {}
  ~result_t();

  std::string name;
  std::vector<line_t> lines;
  int max_prefixlen = 0;
};

int result_add(struct result_t *result, char *repo, char *entry, int prefixlen);
size_t result_print(struct result_t *result, int prefixlen, char eol);
int results_get_prefixlen(struct result_t **results, int count);

/* vim: set ts=2 sw=2 et: */
