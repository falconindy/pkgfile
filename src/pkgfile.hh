#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "db.hh"
#include "filter.hh"
#include "mapped_repo.hh"
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
  };

  Pkgfile(Options options);
  ~Pkgfile() {}

  int Run(const std::vector<std::string>& args);

 private:
  struct ParsedPkgname {
    std::string_view name;
    std::string_view version;
  };

  std::string FormatSearchResult(std::string_view repo,
                                 const ParsedPkgname& pkg);

  // Normalizes the raw query text before it's used to search or build a
  // filter, e.g. appending ".exe" on Windows for exact binary searches.
  std::string NormalizeQuery(std::string query) const;

  std::unique_ptr<filter::Filter> BuildFilterFromOptions(const Options& config,
                                                         std::string query);

  std::vector<const db::MappedRepo*> SelectRepos(const Database& database,
                                                 std::string_view reponame);

  int RunSearch(const Database& database, std::string_view reponame,
                std::string_view query);
  int RunList(const Database& database, std::string_view reponame,
              std::string_view query);

  // -- search mode --

  void SearchExactIndexed(const db::MappedRepo& repo, std::string_view query,
                          Result* result);
  void SearchBasenameIndexed(const db::MappedRepo& repo,
                             std::span<const db::Posting> postings,
                             Result* result);
  void SearchFullPathIndexed(const db::MappedRepo& repo, std::string_view query,
                             Result* result);
  bool PathMatches(const db::MappedRepo& repo, uint32_t tagged_path,
                   std::string_view query) const;

  void ScanCaseInsensitive(const db::MappedRepo& repo, std::string_view query,
                           Result* result);
  void ScanAllFiles(const db::MappedRepo& repo, const filter::Filter& filter,
                    size_t pkg_begin, size_t pkg_end, Result* result);

  // A [begin, end) slice of one repo's package table, sized so that scanning
  // it takes roughly the same work as any other chunk -- see
  // BuildScanChunks().
  struct ScanChunk {
    const db::MappedRepo* repo;
    size_t begin;
    size_t end;
  };

  // Splits every repo's package table into chunks and flattens them into one
  // list spanning all of `repos`, so a scan over a single (large) repo can
  // still be spread across every worker thread rather than being stuck on
  // one thread merely because it's the only repo in play. Each repo's
  // packages are split so that the sum of `weight(repo, pkg_index)` per
  // chunk is roughly `total_weight / (hardware_concurrency * 4)` --
  // oversubscribing a bit so chunks can be load-balanced via work stealing.
  std::vector<ScanChunk> BuildScanChunks(
      std::span<const db::MappedRepo* const> repos,
      const std::function<size_t(const db::MappedRepo&, size_t pkg_index)>&
          weight);

  // Runs `work` over every chunk in `chunks`, dispatched across a pool of
  // worker threads via a shared queue (rather than a static split) so chunks
  // from small repos and large repos even out.
  void RunOverChunks(std::vector<ScanChunk> chunks,
                     const std::function<void(const ScanChunk&)>& work);

  // -- list mode --

  void EmitPackageFileList(const db::MappedRepo& repo, const db::Package& pkg,
                           Result* result);
  void ListCaseInsensitive(const db::MappedRepo& repo, std::string_view query,
                           Result* result);
  void ListScan(const db::MappedRepo& repo, const filter::Filter& filter,
                size_t pkg_begin, size_t pkg_end, Result* result);

  // Runs `work` across [0, count) partitioned into ranges, one per worker
  // thread, and blocks until all workers finish.
  void ParallelFor(size_t count,
                   const std::function<void(size_t begin, size_t end)>& work);

  Options options_;
  std::vector<std::string> bins_;
};

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
