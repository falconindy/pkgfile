#pragma once

#include <filesystem>
#include <map>
#include <optional>

#include "archive_io.hh"
#include "archive_reader.hh"
#include "db.hh"
#include "filter.hh"
#include "repo.hh"
#include "result.hh"

namespace pkgfile {

enum class FilterStyle {
  EXACT,
  GLOB,
  REGEX,
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

class Pkgfile {
 public:
  struct Options {
    Options() {}

    std::string cfgfile = DEFAULT_PACMAN_CONF;
    std::string cachedir = DEFAULT_CACHEPATH;
    std::string targetrepo;

    FilterStyle filterby = FilterStyle::EXACT;
    Mode mode = MODE_SEARCH;

    bool binaries = false;
    bool directories = false;
    bool case_sensitive = true;
    bool quiet = false;
    bool verbose = false;
    bool raw = false;
    char eol = '\n';
    int repo_chunk_bytes = -1;  // <=0 implies default chunk size
  };

  Pkgfile(Options options);
  ~Pkgfile() {}

  int Run(const std::vector<std::string>& args);

 private:
  struct Package {
    std::string_view name;
    std::string_view version;
  };

  using ArchiveEntryCallback = std::function<int(
      const std::string& repo, const filter::Filter& filter, const Package& pkg,
      Result* result, const PackageMeta::FileList& files)>;

  std::unique_ptr<filter::Filter> BuildFilterFromOptions(
      const Options& config, const std::string& match);

  static bool ParsePkgname(Pkgfile::Package* pkg, std::string_view entryname);

  void ProcessRepo(const std::string& reponame, const std::string& repopath,
                   const filter::Filter& filter, Result* result);

  std::string FormatSearchResult(const std::string& repo, const Package& pkg);

  int SearchRepos(Database::RepoChunks repo_chunks,
                  const filter::Filter& filter);
  int SearchSingleRepo(const Database& db, const filter::Filter& filter,
                       std::string_view searchstring);

  int SearchMetafile(const std::string& repo, const filter::Filter& filter,
                     const Package& pkg, Result* result,
                     const PackageMeta::FileList& files);
  int ListMetafile(const std::string& repo, const filter::Filter& filter,
                   const Package& pkg, Result* result,
                   const PackageMeta::FileList& files);

  Options options_;
  ArchiveEntryCallback entry_callback_;
  bool try_mmap_;

  std::vector<std::string> bins_;
};

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
