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
  MODE_UNSPECIFIED = 0x0,

  MODE_QUERY = 0x10,
  MODE_SEARCH = 0x11,
  MODE_LIST = 0x12,

  MODE_UPDATE = 0x20,
  MODE_UPDATE_ASNEEDED = 0x21,
  MODE_UPDATE_FORCE = 0x22
};

namespace pkgfile {

using ArchiveEntryCallback = int (*)(const std::string& repo,
                                     const pkgfile::filter::Filter& filter,
                                     const Package& pkg,
                                     pkgfile::Result* result,
                                     pkgfile::ArchiveReader* reader);

}

struct config_t {
  const char* cfgfile;
  const char* cachedir;
  filterstyle_t filterby;
  pkgfile::ArchiveEntryCallback filefunc;
  Mode mode;
  const char* targetrepo;
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
