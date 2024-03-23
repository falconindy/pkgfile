#pragma once

#include <limits.h>
#include <stdbool.h>

#include <archive.h>
#include <archive_entry.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "result.h"

struct memblock_t {
  char *base;
  char *offset;
  size_t size;
};

struct archive_line_reader {
  struct memblock_t line;
  struct memblock_t block;

  long ret;
};

typedef enum _filterstyle_t {
  FILTER_EXACT = 0,
  FILTER_GLOB,
  FILTER_REGEX
} filterstyle_t;

typedef union _filterpattern_t {
  struct pcre_data {
    pcre2_code *re;
  } re;
  struct glob_data {
    char *glob;
    int globlen;
  } glob;
} filterpattern_t;

struct pkg_t {
  char name[PATH_MAX];
  const char *version;
  int namelen;
};

struct config_t {
  const char *cfgfile;
  const char *cachedir;
  filterstyle_t filterby;
  filterpattern_t filter;
  int (*filefunc)(const char *repo, struct pkg_t *pkg, struct archive *a,
                  struct result_t *result, struct archive_line_reader *buf);
  int (*filterfunc)(const filterpattern_t *filter, const char *line, int len,
                    int flags);
  void (*filterfree)(filterpattern_t *filter);
  int doupdate;
  char *targetrepo;
  bool binaries;
  bool directories;
  bool icase;
  int matchflags;
  bool quiet;
  bool verbose;
  bool raw;
  char eol;
  int compress;
};

int reader_getline(struct archive_line_reader *b, struct archive *a);

/* vim: set ts=2 sw=2 et: */
