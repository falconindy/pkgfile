#pragma once

#include <sys/types.h>

struct line_t {
  char *prefix;
  char *entry;
};

struct result_t {
  size_t size;
  size_t capacity;
  char *name;
  struct line_t **lines;
  int max_prefixlen;
};

struct result_t *result_new(char *name, size_t initial_size);
int result_add(struct result_t *result, char *repo, char *entry, int prefixlen);
void result_free(struct result_t *result);
size_t result_print(struct result_t *result, int prefixlen, char eol);
int results_get_prefixlen(struct result_t **results, int count);

/* vim: set ts=2 sw=2 et: */
