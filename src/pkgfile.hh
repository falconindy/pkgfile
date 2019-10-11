#pragma once

#include "archive_reader.hh"
#include "filter.hh"
#include "result.hh"

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
  int (*filefunc)(const std::string& repo,
                  const pkgfile::filter::Filter& filter, const Package& pkg,
                  pkgfile::Result* result, pkgfile::ArchiveReader* reader);
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

// vim: set ts=2 sw=2 et:
