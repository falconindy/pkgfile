#pragma once

#include <archive.h>
#include <archive_entry.h>

#include "filter.hh"
#include "result.hh"

struct memblock_t {
  char* base;
  char* offset;
  size_t size;
};

struct archive_line_reader {
  struct memblock_t line;
  struct memblock_t block;

  long ret;
};

enum filterstyle_t { FILTER_EXACT = 0, FILTER_GLOB, FILTER_REGEX };

struct Package {
  std::string_view name;
  std::string_view version;
};

enum Mode {
  MODE_UNSPECIFIED,
  MODE_SEARCH,
  MODE_LIST,
};

struct config_t {
  const char* cfgfile;
  const char* cachedir;
  filterstyle_t filterby;
  int (*filefunc)(const char* repo, const pkgfile::filter::Filter& filter,
                  const Package& pkg, archive* a, result_t* result,
                  archive_line_reader* buf);
  Mode mode;
  int doupdate;
  char* targetrepo;
  bool binaries;
  bool directories;
  bool icase;
  bool quiet;
  bool verbose;
  bool raw;
  char eol;
  int compress;
};

int reader_getline(struct archive_line_reader* b, struct archive* a);

// vim: set ts=2 sw=2 et:
