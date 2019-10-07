#pragma once

#include <archive.h>
#include <archive_entry.h>

#include <pcre.h>

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

union filterpattern_t {
  struct pcre_data {
    pcre* re;
    pcre_extra* re_extra;
  } re;
  struct glob_data {
    char* glob;
    size_t globlen;
  } glob;
};

struct Package {
  std::string_view name;
  std::string_view version;
};

struct config_t {
  const char* cfgfile;
  const char* cachedir;
  filterstyle_t filterby;
  filterpattern_t filter;
  int (*filefunc)(const char* repo, const Package& pkg, archive* a,
                  result_t* result, archive_line_reader* buf);
  int (*filterfunc)(const filterpattern_t* filter, std::string_view line,
                    int flags);
  void (*filterfree)(filterpattern_t* filter);
  int doupdate;
  char* targetrepo;
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

int reader_getline(struct archive_line_reader* b, struct archive* a);

// vim: set ts=2 sw=2 et:
