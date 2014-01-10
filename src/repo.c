/*
 * Copyright (C) 2011-2013 by Dave Reisner <dreisner@archlinux.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "macro.h"
#include "repo.h"

struct repo_t *repo_new(const char *reponame) {
  struct repo_t *repo;

  CALLOC(repo, 1, sizeof(struct repo_t), return NULL);

  repo->name = strdup(reponame);
  if (repo->name == NULL) {
    fprintf(stderr, "error: failed to allocate memory\n");
    free(repo);
    return NULL;
  }

  /* assume glorious failure */
  repo->err = 1;

  return repo;
}

void repo_free(struct repo_t *repo) {
  free(repo->name);
  for (int i = 0; i < repo->servercount; ++i) {
    free(repo->servers[i]);
  }
  free(repo->servers);

  free(repo);
}

static int repos_add_repo(struct repovec_t *repos, const char *name) {
  if (repos->size == repos->capacity) {
    int new_capacity = repos->size * 2.5;
    struct repo_t **new_repos =
        realloc(repos->repos, sizeof(struct repo_t *) * new_capacity);
    if (new_repos == NULL) {
      return -ENOMEM;
    }

    repos->repos = new_repos;
    repos->capacity = new_capacity;
  }

  repos->repos[repos->size] = repo_new(name);
  if (repos->repos[repos->size] == NULL) {
    return -ENOMEM;
  }

  repos->size++;

  return 0;
}

static struct repovec_t *repos_new(void) {
  struct repovec_t *repos = calloc(1, sizeof(struct repovec_t));
  if (repos == NULL) {
    return NULL;
  }

  repos->repos = malloc(10 * sizeof(struct repo_t *));
  if (repos->repos == NULL) {
    free(repos);
    return NULL;
  }

  repos->capacity = 10;

  return repos;
}

void repos_free(struct repovec_t *repos) {
  struct repo_t *repo;

  if (repos == NULL) {
    return;
  }

  REPOVEC_FOREACH(repo, repos) { repo_free(repo); }

  free(repos->repos);
  free(repos);
}

int repo_add_server(struct repo_t *repo, const char *server) {
  if (!repo) {
    return 1;
  }

  repo->servers =
      realloc(repo->servers, sizeof(char *) * (repo->servercount + 1));

  repo->servers[repo->servercount] = strdup(server);
  repo->servercount++;

  return 0;
}

static size_t strtrim(char *str) {
  char *left = str, *right;

  if (!str || *str == '\0') {
    return 0;
  }

  while (isspace((unsigned char)*left)) {
    left++;
  }
  if (left != str) {
    memmove(str, left, (strlen(left) + 1));
  }

  if (*str == '\0') {
    return 0;
  }

  right = (char *)rawmemchr(str, '\0') - 1;
  while (isspace((unsigned char)*right)) {
    right--;
  }
  *++right = '\0';

  return right - left;
}

static char *split_keyval(char *line, const char *sep) {
  strsep(&line, sep);
  return line;
}

static int parse_one_file(const char *, char **, struct repovec_t *);

static int parse_include(const char *include, char **section,
                         struct repovec_t *repos) {
  glob_t globbuf;

  if (glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include);
    return -ENOMEM;
  }

  for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
    parse_one_file(globbuf.gl_pathv[i], section, repos);
  }

  globfree(&globbuf);

  return 0;
}

static int parse_one_file(const char *filename, char **section,
                          struct repovec_t *repos) {
  FILE *fp;
  char *ptr;
  char line[4096];
  const char *const server = "Server";
  const char *const include = "Include";
  int in_options = 0, r = 0, lineno = 0;

  fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "error: failed to open %s: %s\n", filename,
            strerror(errno));
    return -errno;
  }

  while (fgets(line, sizeof(line), fp)) {
    size_t len;
    ++lineno;

    /* remove comments */
    ptr = strchr(line, '#');
    if (ptr) {
      *ptr = '\0';
    }

    len = strtrim(line);
    if (len == 0) {
      continue;
    }

    /* found a section header */
    if (line[0] == '[' && line[len - 1] == ']') {
      free(*section);
      *section = strndup(&line[1], len - 2);
      in_options = len - 2 == 7 && memcmp(*section, "options", 7) == 0;
      if (!in_options) {
        repos_add_repo(repos, *section);
      }
    }

    if (memchr(line, '=', len)) {
      char *key = line, *val = split_keyval(line, "=");
      size_t keysz = strtrim(key);
      strtrim(val);

      if (keysz == strlen(server) && memcmp(key, server, keysz) == 0) {
        if (*section == NULL) {
          fprintf(
              stderr,
              "error: failed to parse %s on line %d: found 'Server' directive "
              "outside of a section\n",
              filename, lineno);
          continue;
        }
        if (in_options) {
          fprintf(
              stderr,
              "error: failed to parse %s on line %d: found 'Server' directive "
              "in options section\n",
              filename, lineno);
          continue;
        }
        r = repo_add_server(repos->repos[repos->size - 1], val);
        if (r < 0) {
          break;
        }
      } else if (keysz == strlen(include) && memcmp(key, include, keysz) == 0) {
        parse_include(val, section, repos);
      }
    }
  }

  fclose(fp);

  return r;
}

struct repovec_t *load_repos_from_file(const char *filename) {
  char *section = NULL;
  struct repovec_t *repos = repos_new();

  if (parse_one_file(filename, &section, repos) < 0) {
    repos_free(repos);
    return NULL;
  }

  free(section);

  return repos;
}

/* vim: set ts=2 sw=2 et: */
