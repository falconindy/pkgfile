#include <string.h>

#include "macro.h"
#include "result.h"

static void line_free(struct line_t *line) {
  if (!line) {
    return;
  }

  free(line->prefix);
  free(line->entry);
  free(line);
}

static struct line_t *line_new(char *prefix, char *entry) {
  struct line_t *line = calloc(1, sizeof(struct line_t));
  if (line == NULL) {
    goto alloc_fail;
  }

  line->prefix = strdup(prefix);
  if (line->prefix == NULL) {
    goto alloc_fail;
  }

  if (entry) {
    line->entry = strdup(entry);
    if (line->entry == NULL) {
      goto alloc_fail;
    }
  }

  return line;

alloc_fail:
  fputs("error: failed to allocate memory for result line\n", stderr);
  line_free(line);
  return NULL;
}

static int result_grow(struct result_t *result) {
  size_t newsz = result->capacity * 3;
  result->lines = realloc(result->lines, newsz * sizeof(struct line_t *));
  if (!result->lines) {
    return 1;
  }

  result->capacity = newsz;

  return 0;
}

struct result_t *result_new(char *name, size_t initial_size) {
  struct result_t *result = calloc(1, sizeof(struct result_t));
  if (result == NULL) {
    goto alloc_fail;
  }

  result->lines = calloc(initial_size, sizeof(struct line_t *));
  if (!result->lines) {
    goto alloc_fail;
  }

  result->name = strdup(name);
  if (result->name == NULL) {
    goto alloc_fail;
  }

  result->capacity = initial_size;
  return result;

alloc_fail:
  fputs("error: failed to allocate memory for result\n", stderr);
  result_free(result);
  return NULL;
}

int result_add(struct result_t *result, char *prefix, char *entry,
               int prefixlen) {
  if (!result || !prefix) {
    return 1;
  }

  if (result->size + 1 >= result->capacity) {
    if (result_grow(result) != 0) {
      return 1;
    }
  }

  if (prefixlen > result->max_prefixlen) {
    result->max_prefixlen = prefixlen;
  }

  result->lines[result->size] = line_new(prefix, entry);
  result->size++;

  return 0;
}

void result_free(struct result_t *result) {
  if (!result) {
    return;
  }

  if (result->lines) {
    for (size_t i = 0; i < result->size; ++i) {
      line_free(result->lines[i]);
    }
    free(result->lines);
  }
  free(result->name);
  free(result);
}

static int linecmp(const void *l1, const void *l2) {
  const struct line_t *line1 = *(struct line_t **)l1;
  const struct line_t *line2 = *(struct line_t **)l2;
  int cmp = strcmp(line1->prefix, line2->prefix);

  if (cmp == 0 && line1->entry && line2->entry) {
    return strcmp(line1->entry, line2->entry);
  } else {
    return cmp;
  }
}

static void result_print_two_columns(struct result_t *result, int prefixlen,
                                     char eol) {
  for (size_t i = 0; i < result->size; ++i) {
    printf("%-*s\t%s%c", prefixlen, result->lines[i]->prefix,
           result->lines[i]->entry, eol);
  }
}

static void result_print_one_column(struct result_t *result, char eol) {
  for (size_t i = 0; i < result->size; ++i) {
    printf("%s%c", result->lines[i]->prefix, eol);
  }
}

size_t result_print(struct result_t *result, int prefixlen, char eol) {
  if (!result->size) {
    return 0;
  }

  qsort(result->lines, result->size, sizeof(char *), linecmp);

  /* It's expected that results are homogenous. */
  if (result->lines[0]->entry) {
    result_print_two_columns(result, prefixlen, eol);
  } else {
    result_print_one_column(result, eol);
  }

  return result->size;
}

int results_get_prefixlen(struct result_t **results, int count) {
  int maxlen = 0;

  for (int i = 0; i < count; ++i) {
    maxlen = MAX(maxlen, results[i]->max_prefixlen);
  }

  return maxlen;
}

/* vim: set ts=2 sw=2 et: */
