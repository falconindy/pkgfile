#include "pkgfile.hh"

#include <getopt.h>
#include <locale.h>
#include <string.h>

#include <algorithm>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include "db.hh"
#include "filter.hh"
#include "queue.hh"
#include "result.hh"
#include "update.hh"

namespace fs = std::filesystem;

namespace pkgfile {

namespace {

bool IsValidBin(std::string_view path) {
  if (path.empty() || path.starts_with("/home") || !path.starts_with('/')) {
    return false;
  }

  return true;
}

std::string WeaklyCanonicalizeBin(std::string_view path) {
  if (!IsValidBin(path)) {
    return std::string();
  }

  std::string canonical(path);

  if (!canonical.ends_with('/')) {
    canonical.append("/");
  }

  // Silently ignore any errors and just return an empty string.
  // ref: https://github.com/falconindy/pkgfile/issues/79
  std::error_code ec;
  return fs::weakly_canonical(canonical, ec);
}

std::vector<std::string> ParsePathVarToBins(const char* var) {
  if (var == nullptr) {
    return {};
  }

  std::set<std::string> bins;
  std::string_view psv(var);

  while (!psv.empty()) {
    auto pos = psv.find(':');
    if (pos == psv.npos) {
      if (std::string canon = WeaklyCanonicalizeBin(psv); !canon.empty()) {
        // then the remainder goes in the vector
        bins.emplace(canon);
      }
      break;
    }

    std::string_view component(psv.data(), pos);
    if (std::string canon = WeaklyCanonicalizeBin(component); !canon.empty()) {
      bins.emplace(canon);
    }

    psv.remove_prefix(pos + 1);
  }

  return {std::make_move_iterator(bins.begin()),
          std::make_move_iterator(bins.end())};
}

}  // namespace

Pkgfile::Pkgfile(Options options) : options_(options) {
  bins_ = ParsePathVarToBins(getenv("PATH"));
}

std::string Pkgfile::FormatSearchResult(std::string_view repo,
                                        const ParsedPkgname& pkg) {
  if (options_.verbose) {
    return std::format("{}/{} {}", repo, pkg.name, pkg.version);
  }

  if (options_.quiet) {
    return std::string(pkg.name);
  }

  return std::format("{}/{}", repo, pkg.name);
}

std::string Pkgfile::NormalizeQuery(std::string query) const {
#ifdef _WIN32
  // In Windows-like environments, binaries can be executed by name without
  // their .exe suffix. Provide the same allowance to pkgfile. This mainly
  // facilitates the cmd-not-found hooks, but direct calls to `pkgfile -b` can
  // benefit as well.
  if (options_.mode == MODE_SEARCH && options_.binaries &&
      options_.filterby == FilterStyle::EXACT &&
      !std::string_view(query).ends_with(".exe")) {
    query.append(".exe");
  }
#endif

  return query;
}

std::unique_ptr<filter::Filter> Pkgfile::BuildFilterFromOptions(
    const Pkgfile::Options& options, std::string query) {
  std::unique_ptr<filter::Filter> filter;

  switch (options.filterby) {
    case FilterStyle::EXACT:
      filter = std::make_unique<filter::Exact>(query, options.case_sensitive);
      break;
    case FilterStyle::GLOB:
      filter = std::make_unique<filter::Glob>(query, options.case_sensitive);
      break;
    case FilterStyle::REGEX:
      filter = filter::Regex::Compile(query, options.case_sensitive);
      if (filter == nullptr) {
        return nullptr;
      }
      break;
  }

  if (options.mode == MODE_SEARCH) {
    if (options.binaries) {
      filter = std::make_unique<filter::And>(
          std::move(filter), std::make_unique<filter::Bin>(bins_));
    }

    if (!options.directories) {
      filter = std::make_unique<filter::And>(
          std::make_unique<filter::Not>(std::make_unique<filter::Directory>()),
          std::move(filter));
    }
  }

  return filter;
}

std::vector<const db::MappedRepo*> Pkgfile::SelectRepos(
    const Database& database, std::string_view reponame) {
  std::vector<const db::MappedRepo*> repos;

  if (reponame.empty()) {
    for (const auto& repo : database.GetAllRepos()) {
      repos.push_back(repo.get());
    }
  } else if (const auto* repo = database.GetRepo(reponame)) {
    repos.push_back(repo);
  }

  return repos;
}

void Pkgfile::ParallelFor(
    size_t count, const std::function<void(size_t begin, size_t end)>& work) {
  if (count == 0) {
    return;
  }

  const size_t num_workers = std::max<size_t>(
      1, std::min<size_t>(std::thread::hardware_concurrency(), count));
  const size_t chunk = (count + num_workers - 1) / num_workers;

  std::vector<std::thread> workers;
  workers.reserve(num_workers);
  for (size_t i = 0; i < num_workers; ++i) {
    const size_t begin = i * chunk;
    const size_t end = std::min(count, begin + chunk);
    if (begin >= end) {
      break;
    }
    workers.emplace_back(work, begin, end);
  }

  for (auto& worker : workers) {
    worker.join();
  }
}

std::vector<Pkgfile::ScanChunk> Pkgfile::BuildScanChunks(
    std::span<const db::MappedRepo* const> repos,
    const std::function<size_t(const db::MappedRepo&, size_t)>& weight) {
  size_t total = 0;
  for (const auto* repo : repos) {
    const size_t count = repo->packages().size();
    for (size_t i = 0; i < count; ++i) {
      total += weight(*repo, i);
    }
  }

  const size_t num_workers =
      std::max<size_t>(1, std::thread::hardware_concurrency());
  const size_t target = std::max<size_t>(1, total / (num_workers * 4));

  std::vector<ScanChunk> chunks;
  for (const auto* repo : repos) {
    const size_t count = repo->packages().size();
    size_t begin = 0;
    size_t running = 0;
    for (size_t i = 0; i < count; ++i) {
      running += weight(*repo, i);
      if (running >= target) {
        chunks.push_back(ScanChunk{repo, begin, i + 1});
        begin = i + 1;
        running = 0;
      }
    }
    if (begin < count) {
      chunks.push_back(ScanChunk{repo, begin, count});
    }
  }

  return chunks;
}

void Pkgfile::RunOverChunks(std::vector<ScanChunk> chunks,
                            const std::function<void(const ScanChunk&)>& work) {
  if (chunks.empty()) {
    return;
  }

  ThreadSafeQueue<ScanChunk> queue;
  for (auto& chunk : chunks) {
    queue.enqueue(std::move(chunk));
  }

  const size_t num_workers = std::min<size_t>(
      std::max<size_t>(1, std::thread::hardware_concurrency()), queue.size());

  std::vector<std::thread> workers;
  workers.reserve(num_workers);
  for (size_t i = 0; i < num_workers; ++i) {
    workers.emplace_back([&] {
      while (auto chunk = queue.try_dequeue()) {
        work(*chunk);
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }
}

// -- search mode --

void Pkgfile::SearchBasenameIndexed(const db::MappedRepo& repo,
                                    std::span<const db::Posting> postings,
                                    Result* result) {
  const filter::Bin is_bin(bins_);
  std::optional<db::PkgId> last_emitted;
  std::string resolved;

  for (const auto& posting : postings) {
    if (db::IsDirOf(posting.path) && !options_.directories) {
      continue;
    }
    if (!options_.verbose && last_emitted == posting.pkg) {
      continue;
    }

    if (options_.binaries || options_.verbose) {
      repo.ResolvePathInto(posting.path, &resolved);
      if (options_.binaries && !is_bin.Matches(resolved)) {
        continue;
      }
    }

    const auto& pkg = repo.packages()[posting.pkg];
    result->Add(
        FormatSearchResult(repo.reponame(), {repo.ResolveString(pkg.name),
                                             repo.ResolveString(pkg.version)}),
        options_.verbose ? resolved : std::string());
    last_emitted = posting.pkg;
  }
}

bool Pkgfile::PathMatches(const db::MappedRepo& repo, uint32_t tagged_path,
                          std::string_view query) const {
  db::PathId id = db::PathIdOf(tagged_path);
  std::string_view remaining = query;

  for (;;) {
    const auto slash = remaining.rfind('/');
    const std::string_view component =
        slash == remaining.npos ? remaining : remaining.substr(slash + 1);

    if (id == db::kRootPath) {
      return false;
    }

    const db::PathNode& node = repo.PathNodeAt(id);
    if (repo.ResolveString(node.name) != component) {
      return false;
    }
    id = node.parent;

    if (slash == remaining.npos) {
      break;
    }
    remaining = remaining.substr(0, slash);
  }

  return id == db::kRootPath;
}

void Pkgfile::SearchFullPathIndexed(const db::MappedRepo& repo,
                                    std::string_view query, Result* result) {
  if (!query.starts_with('/')) {
    // Every stored path has a leading slash, so a query without one can
    // never match.
    return;
  }
  query.remove_prefix(1);

  bool want_dir = false;
  if (query.ends_with('/')) {
    want_dir = true;
    query.remove_suffix(1);
  }

  if (query.empty()) {
    return;
  }

  if (want_dir && !options_.directories) {
    // A directory-style query can only ever match a directory occurrence,
    // and those are excluded entirely unless -d was given.
    return;
  }

  const auto last_slash = query.rfind('/');
  const std::string_view basename =
      last_slash == query.npos ? query : query.substr(last_slash + 1);

  const auto* entry = repo.FindBasename(basename);
  if (entry == nullptr) {
    return;
  }

  const filter::Bin is_bin(bins_);
  std::string resolved;

  db::Posting scratch;
  for (const auto& posting : repo.PostingsFor(*entry, &scratch)) {
    if (db::IsDirOf(posting.path) != want_dir) {
      continue;
    }
    if (!PathMatches(repo, posting.path, query)) {
      continue;
    }

    repo.ResolvePathInto(posting.path, &resolved);
    if (options_.binaries && !is_bin.Matches(resolved)) {
      continue;
    }

    const auto& pkg = repo.packages()[posting.pkg];
    result->Add(
        FormatSearchResult(repo.reponame(), {repo.ResolveString(pkg.name),
                                             repo.ResolveString(pkg.version)}),
        options_.verbose ? resolved : std::string());
  }
}

void Pkgfile::SearchExactIndexed(const db::MappedRepo& repo,
                                 std::string_view query, Result* result) {
  if (query.find('/') == query.npos) {
    if (const auto* entry = repo.FindBasename(query)) {
      db::Posting scratch;
      SearchBasenameIndexed(repo, repo.PostingsFor(*entry, &scratch), result);
    }
  } else {
    SearchFullPathIndexed(repo, query, result);
  }
}

void Pkgfile::ScanExactCaseInsensitive(const db::MappedRepo& repo,
                                       std::string_view query, Result* result) {
  const bool full_path_query = query.find('/') != query.npos;

  // The basename of whatever this query is looking for -- for a bare query
  // like "foo" that's the whole thing; for a full-path query like
  // "/usr/bin/foo" or a directory query like "/usr/bin/" (trailing slash
  // stripped first) it's the last path component.
  std::string_view for_basename = query;
  if (for_basename.ends_with('/')) {
    for_basename.remove_suffix(1);
  }
  const auto last_slash = for_basename.rfind('/');
  const std::string_view query_basename =
      last_slash == for_basename.npos ? for_basename
                                      : for_basename.substr(last_slash + 1);

  const filter::Bin is_bin(bins_);
  std::vector<bool> emitted(repo.packages().size(), false);
  std::string resolved;

  for (const auto& entry : repo.basename_index()) {
    const std::string_view name = repo.ResolveString(entry.name);
    if (name.size() != query_basename.size() ||
        strncasecmp(name.data(), query_basename.data(), name.size()) != 0) {
      continue;
    }

    db::Posting scratch;
    for (const auto& posting : repo.PostingsFor(entry, &scratch)) {
      if (!options_.verbose && emitted[posting.pkg]) {
        continue;
      }
      if (db::IsDirOf(posting.path) && !options_.directories) {
        continue;
      }

      repo.ResolvePathInto(posting.path, &resolved);

      // Full-path query (e.g. "/usr/bin/foo" or "/usr/bin/"): the basename
      // already matched above, but the rest of the path -- including a
      // trailing slash for a directory query -- must too.
      if (full_path_query &&
          (resolved.size() != query.size() ||
           strncasecmp(resolved.data(), query.data(), query.size()) != 0)) {
        continue;
      }

      if (options_.binaries && !is_bin.Matches(resolved)) {
        continue;
      }

      const auto& pkg = repo.packages()[posting.pkg];
      result->Add(FormatSearchResult(repo.reponame(),
                                     {repo.ResolveString(pkg.name),
                                      repo.ResolveString(pkg.version)}),
                  options_.verbose ? resolved : std::string());
      emitted[posting.pkg] = true;
    }
  }
}

void Pkgfile::ScanAllFiles(const db::MappedRepo& repo,
                           const filter::Filter& filter, size_t pkg_begin,
                           size_t pkg_end, Result* result) {
  const auto pkgs = repo.packages();
  std::string resolved;

  for (size_t i = pkg_begin; i < pkg_end; ++i) {
    const auto& pkg = pkgs[i];
    bool emitted = false;

    for (const auto tagged_path : repo.PackageFiles(pkg)) {
      if (!options_.verbose && emitted) {
        break;
      }

      repo.ResolvePathInto(tagged_path, &resolved);
      if (!filter.Matches(resolved)) {
        continue;
      }

      result->Add(FormatSearchResult(repo.reponame(),
                                     {repo.ResolveString(pkg.name),
                                      repo.ResolveString(pkg.version)}),
                  options_.verbose ? resolved : std::string());
      emitted = true;
    }
  }
}

// -- list mode --

void Pkgfile::EmitPackageFileList(const db::MappedRepo& repo,
                                  const db::Package& pkg, Result* result) {
  const filter::Bin is_bin(bins_);

  for (const auto tagged_path : repo.PackageFiles(pkg)) {
    std::string resolved;
    repo.ResolvePathInto(tagged_path, &resolved);
    if (options_.binaries && !is_bin.Matches(resolved)) {
      continue;
    }

    if (options_.quiet) {
      result->Add(std::move(resolved), std::string());
    } else {
      result->Add(
          std::format("{}/{}", repo.reponame(), repo.ResolveString(pkg.name)),
          std::move(resolved));
    }
  }
}

void Pkgfile::ListExactCaseInsensitive(const db::MappedRepo& repo,
                                       std::string_view query, Result* result) {
  for (const auto& pkg : repo.packages()) {
    const auto name = repo.ResolveString(pkg.name);
    if (name.size() == query.size() &&
        strncasecmp(name.data(), query.data(), query.size()) == 0) {
      EmitPackageFileList(repo, pkg, result);
      return;
    }
  }
}

void Pkgfile::ListScan(const db::MappedRepo& repo, const filter::Filter& filter,
                       size_t pkg_begin, size_t pkg_end, Result* result) {
  const auto pkgs = repo.packages();

  for (size_t i = pkg_begin; i < pkg_end; ++i) {
    const auto& pkg = pkgs[i];
    if (filter.Matches(repo.ResolveString(pkg.name))) {
      EmitPackageFileList(repo, pkg, result);
    }
  }
}

// -- top level --

int Pkgfile::RunSearch(const Database& database, std::string_view reponame,
                       std::string_view query_in) {
  const std::string query = NormalizeQuery(std::string(query_in));
  const auto repos = SelectRepos(database, reponame);

  const bool use_index =
      options_.filterby == FilterStyle::EXACT && options_.case_sensitive;

  std::unique_ptr<filter::Filter> filter;
  if (!use_index && options_.filterby != FilterStyle::EXACT) {
    filter = BuildFilterFromOptions(options_, query);
    if (filter == nullptr) {
      return 1;
    }
  }

  std::map<std::string, std::unique_ptr<Result>> results;
  for (const auto* repo : repos) {
    results.emplace(std::string(repo->reponame()), std::make_unique<Result>());
  }
  auto result_for = [&](const db::MappedRepo& repo) {
    return results[std::string(repo.reponame())].get();
  };

  if (use_index) {
    ParallelFor(repos.size(), [&](size_t begin, size_t end) {
      for (size_t i = begin; i < end; ++i) {
        SearchExactIndexed(*repos[i], query, result_for(*repos[i]));
      }
    });
  } else if (options_.filterby == FilterStyle::EXACT) {
    // Case-insensitive exact/basename search: the index is sorted
    // case-sensitively, so fall back to scanning the (much smaller) set of
    // distinct basenames instead of every file.
    ParallelFor(repos.size(), [&](size_t begin, size_t end) {
      for (size_t i = begin; i < end; ++i) {
        ScanExactCaseInsensitive(*repos[i], query, result_for(*repos[i]));
      }
    });
  } else {
    // Glob/regex has to scan every file, which for a single large repo can
    // dwarf what one thread can chew through. Partition by total file count
    // across every selected repo combined -- rather than one work item per
    // repo -- so a search scoped to a single repo still saturates every
    // core.
    auto chunks = BuildScanChunks(repos, [](const db::MappedRepo& r, size_t i) {
      return static_cast<size_t>(r.packages()[i].files_count);
    });

    RunOverChunks(std::move(chunks), [&](const ScanChunk& chunk) {
      ScanAllFiles(*chunk.repo, *filter, chunk.begin, chunk.end,
                   result_for(*chunk.repo));
    });
  }

  std::erase_if(results, [](auto& result) { return result.second->Empty(); });

  if (results.empty()) {
    return 1;
  }

  const size_t prefixlen =
      options_.raw ? 0
                   : std::max_element(results.begin(), results.end(),
                                      [](const auto& a, const auto& b) {
                                        return a.second->MaxPrefixlen() <
                                               b.second->MaxPrefixlen();
                                      })
                         ->second->MaxPrefixlen();

  for (auto& [repo, result] : results) {
    result->Print(prefixlen, options_.eol);
  }

  return 0;
}

int Pkgfile::RunList(const Database& database, std::string_view reponame,
                     std::string_view query) {
  const auto repos = SelectRepos(database, reponame);

  const bool use_index =
      options_.filterby == FilterStyle::EXACT && options_.case_sensitive;

  std::unique_ptr<filter::Filter> filter;
  if (!use_index) {
    filter = BuildFilterFromOptions(options_, std::string(query));
    if (filter == nullptr) {
      return 1;
    }
  }

  Result result;

  if (use_index) {
    for (const auto* repo : repos) {
      if (const auto* pkg = repo->FindPackageByName(query)) {
        EmitPackageFileList(*repo, *pkg, &result);
      }
    }
  } else if (options_.filterby == FilterStyle::EXACT) {
    for (const auto* repo : repos) {
      ListExactCaseInsensitive(*repo, query, &result);
    }
  } else {
    // Same reasoning as the search-mode glob/regex path: partition by
    // package count across every selected repo combined so a listing scoped
    // to one repo can still use every core.
    auto chunks = BuildScanChunks(
        repos, [](const db::MappedRepo&, size_t) { return size_t{1}; });

    RunOverChunks(std::move(chunks), [&](const ScanChunk& chunk) {
      ListScan(*chunk.repo, *filter, chunk.begin, chunk.end, &result);
    });
  }

  if (result.Empty()) {
    return 1;
  }

  result.Print(options_.raw ? 0 : result.MaxPrefixlen(), options_.eol);

  return 0;
}

int Pkgfile::Run(const std::vector<std::string>& args) {
  if (options_.mode & MODE_UPDATE) {
    return Updater(options_.cachedir)
        .Update(options_.cfgfile, options_.mode == MODE_UPDATE_FORCE);
  }

  if (args.empty()) {
    std::cerr << "error: no target specified (use -h for help)\n";
    return 1;
  }

  std::error_code ec;
  const auto database = Database::Open(options_.cachedir, ec);
  if (ec.value() != 0) {
    std::cerr << std::format("error: Failed to open cache directory {}: {}{}\n",
                             options_.cachedir, ec.message(),
                             DatabaseError::Is(ec)
                                 ? " (you may need to run `pkgfile --update`)"
                                 : "");
    return 1;
  } else if (database->empty()) {
    std::cerr << "error: No repo files found. Please run `pkgfile --update.\n";
    return 1;
  }

  auto ParseQueryInput = [this](std::string_view input)
      -> std::pair<std::string_view, std::string_view> {
    const auto pos = input.find('/');

    // Make sure we reject anything that starts with a slash.
    if (options_.mode == MODE_LIST && pos != input.npos && pos > 0) {
      return {input.substr(0, pos), input.substr(pos + 1)};
    } else if (!options_.targetrepo.empty()) {
      return {options_.targetrepo, input};
    } else {
      return {std::string_view{}, input};
    }
  };

  auto [repo, query] = ParseQueryInput(args[0]);

  if (options_.mode == MODE_LIST) {
    return RunList(*database, repo, query);
  }

  return RunSearch(*database, repo, query);
}

}  // namespace pkgfile

namespace {

void Usage(void) {
  std::cout << "pkgfile " PACKAGE_VERSION
               "\nUsage: pkgfile [options] target\n\n";
  std::cout <<  //
      " Operations:\n"
      "  -l, --list              list contents of a package\n"
      "  -s, --search            search for packages containing the target "
      "(default)\n"
      "  -u, --update            update repo files lists\n\n";
  std::cout <<  //
      " Matching:\n"
      "  -b, --binaries          return only files contained in a bin dir\n"
      "  -d, --directories       match directories in searches\n"
      "  -g, --glob              enable matching with glob characters\n"
      "  -i, --ignorecase        use case insensitive matching\n"
      "  -R, --repo <repo>       search a singular repo\n"
      "  -r, --regex             enable matching with regular "
      "expressions\n\n";
  std::cout <<  //
      " Output:\n"
      "  -q, --quiet             output less when listing\n"
      "  -v, --verbose           output more\n"
      "  -w, --raw               disable output justification\n"
      "  -0, --null              null terminate output\n\n";
  std::cout <<  //
      " General:\n  -C, --config <file>     use an alternate config (default: "
      "/etc/pacman.conf)\n"
      "  -D, --cachedir <dir>    use an alternate cachedir "
      "(default: " DEFAULT_CACHEPATH
      ")\n"
      "  -h, --help              display this help and exit\n"
      "  -V, --version           display the version and exit\n\n";
}

void Version(void) { std::cout << PACKAGE_NAME " v" PACKAGE_VERSION "\n"; }

std::optional<pkgfile::Pkgfile::Options> ParseOpts(int* argc, char*** argv) {
  static constexpr char kShortOpts[] = "0bC:D:dghilqR:rsuVvwz::";
  static constexpr struct option kLongOpts[] = {
      // clang-format off
    { "binaries",       no_argument,        0, 'b' },
    { "cachedir",       required_argument,  0, 'D' },
    { "compress",       optional_argument,  0, 'z' },
    { "config",         required_argument,  0, 'C' },
    { "directories",    no_argument,        0, 'd' },
    { "glob",           no_argument,        0, 'g' },
    { "help",           no_argument,        0, 'h' },
    { "ignorecase",     no_argument,        0, 'i' },
    { "list",           no_argument,        0, 'l' },
    { "quiet",          no_argument,        0, 'q' },
    { "repo",           required_argument,  0, 'R' },
    { "regex",          no_argument,        0, 'r' },
    { "search",         no_argument,        0, 's' },
    { "update",         no_argument,        0, 'u' },
    { "version",        no_argument,        0, 'V' },
    { "verbose",        no_argument,        0, 'v' },
    { "raw",            no_argument,        0, 'w' },
    { "null",           no_argument,        0, '0' },
    { 0, 0, 0, 0 },
    // clang-format pn
  };

  pkgfile::Pkgfile::Options options;

  for (;;) {
    const int opt = getopt_long(*argc, *argv, kShortOpts, kLongOpts, nullptr);
    if (opt < 0) {
      break;
    }
    switch (opt) {
      case '0':
        options.eol = '\0';
        break;
      case 'b':
        options.binaries = true;
        break;
      case 'C':
        options.cfgfile = optarg;
        break;
      case 'D':
        options.cachedir = optarg;
        break;
      case 'd':
        options.directories = true;
        break;
      case 'g':
        options.filterby = pkgfile::FilterStyle::GLOB;
        break;
      case 'h':
        Usage();
        exit(EXIT_SUCCESS);
      case 'i':
        options.case_sensitive = false;
        break;
      case 'l':
        options.mode = pkgfile::MODE_LIST;
        break;
      case 'q':
        options.quiet = true;
        break;
      case 'R':
        options.targetrepo = optarg;
        break;
      case 'r':
        options.filterby = pkgfile::FilterStyle::REGEX;
        break;
      case 's':
        options.mode = pkgfile::MODE_SEARCH;
        break;
      case 'u':
        if (options.mode & pkgfile::MODE_UPDATE) {
          options.mode = pkgfile::MODE_UPDATE_FORCE;
        } else {
          options.mode = pkgfile::MODE_UPDATE_ASNEEDED;
        }
        break;
      case 'V':
        Version();
        exit(EXIT_SUCCESS);
      case 'v':
        options.verbose = true;
        break;
      case 'w':
        options.raw = true;
        break;
      case 'z':
        // Accepted for backwards compatibility, but silently ignored: the
        // on-disk database is no longer compressed.
        break;
      default:
        return std::nullopt;
    }
  }

  *argc -= optind - 1;
  *argv += optind - 1;

  return options;
}

}  // namespace

int main(int argc, char* argv[]) {
  setlocale(LC_ALL, "");

  const auto options = ParseOpts(&argc, &argv);
  if (options == std::nullopt) {
    return 2;
  }

  const std::vector<std::string> args(argv + 1, argv + argc);
  return pkgfile::Pkgfile(*options).Run(args);
}

// vim: set ts=2 sw=2 et:
