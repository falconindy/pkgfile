#include "pkgfile.hh"

#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <string.h>

#include <format>
#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include "archive_io.hh"
#include "compress.hh"
#include "db.hh"
#include "filter.hh"
#include "queue.hh"
#include "repo.hh"
#include "result.hh"
#include "update.hh"

namespace fs = std::filesystem;

namespace pkgfile {

namespace {

std::string WeaklyCanonicalizeBin(std::string_view path) {
  std::string canonical(path);

  if (!canonical.ends_with('/')) {
    canonical.append("/");
  }

  return fs::weakly_canonical(canonical);
}

}  // namespace

Pkgfile::Pkgfile(Options options) : options_(options) {
  switch (options_.mode) {
    case MODE_SEARCH:
      try_mmap_ = true;
      entry_callback_ = [this](const std::string& repo,
                               const filter::Filter& filter, const Package& pkg,
                               Result* result, ArchiveReader* reader) {
        return SearchMetafile(repo, filter, pkg, result, reader);
      };
      break;
    case MODE_LIST:
      try_mmap_ = false;
      entry_callback_ = [this](const std::string& repo,
                               const filter::Filter& filter, const Package& pkg,
                               Result* result, ArchiveReader* reader) {
        return ListMetafile(repo, filter, pkg, result, reader);
      };
      break;
    default:
      break;
  }

  if (const char* p = getenv("PATH"); p) {
    std::string_view psv(p);

    while (!psv.empty()) {
      auto pos = psv.find(':');
      if (pos == psv.npos) {
        // then the remainder goes in the vector
        bins_.emplace_back(WeaklyCanonicalizeBin(psv));
        break;
      }

      // Remove any trailing slashes from the PATH component and normalize it.
      std::string_view component = {psv.data(), pos};
      while (!component.empty() && component.ends_with('/')) {
        component.remove_suffix(1);
      }

      // No relative paths
      if (!component.empty() && component.starts_with('/') &&
          !component.starts_with("/home")) {
        bins_.emplace_back(WeaklyCanonicalizeBin(component));
      }

      psv.remove_prefix(pos + 1);
    }
  }
}

std::string Pkgfile::FormatSearchResult(const std::string& repo,
                                        const Package& pkg) {
  if (options_.verbose) {
    return std::format("{}/{} {}", repo, pkg.name, pkg.version);
  }

  if (options_.quiet) {
    return std::string(pkg.name);
  }

  return std::format("{}/{}", repo, pkg.name);
}

int Pkgfile::SearchMetafile(const std::string& repo,
                            const pkgfile::filter::Filter& filter,
                            const Package& pkg, pkgfile::Result* result,
                            pkgfile::ArchiveReader* reader) {
  std::string_view line;
  while (reader->GetLine(&line) == ARCHIVE_OK) {
    if (!filter.Matches(line)) {
      continue;
    }

    result->Add(FormatSearchResult(repo, pkg),
                options_.verbose ? std::string(line) : std::string());

    if (!options_.verbose) {
      return 0;
    }
  }

  return 0;
}

int Pkgfile::ListMetafile(const std::string& repo,
                          const pkgfile::filter::Filter& filter,
                          const Package& pkg, pkgfile::Result* result,
                          pkgfile::ArchiveReader* reader) {
  if (!filter.Matches(pkg.name)) {
    return 0;
  }

  const pkgfile::filter::Bin is_bin(bins_);
  std::string_view line;
  while (reader->GetLine(&line) == ARCHIVE_OK) {
    if (options_.binaries && !is_bin.Matches(line)) {
      continue;
    }

    std::string out;
    if (options_.quiet) {
      out = line;
    } else {
      out = std::format("{}/{}", repo, pkg.name);
    }
    result->Add(std::move(out),
                options_.quiet ? std::string() : std::string(line));
  }

  // When we encounter a match with fixed string matching, we know we're done.
  // However, for other filter methods, we can't be sure that our pattern won't
  // produce further matches, so we signal our caller to continue.
  return options_.filterby == FilterStyle::EXACT ? -1 : 0;
}

// static
bool Pkgfile::ParsePkgname(Pkgfile::Package* pkg, std::string_view entryname) {
  const auto pkgrel = entryname.rfind('-');
  if (pkgrel == entryname.npos) {
    return false;
  }

  const auto pkgver = entryname.substr(0, pkgrel).rfind('-');
  if (pkgver == entryname.npos) {
    return false;
  }

  pkg->name = entryname.substr(0, pkgver);
  pkg->version = entryname.substr(pkgver + 1);

  return true;
}

void Pkgfile::ProcessRepo(const std::string& reponame,
                          const std::string& repopath,
                          const filter::Filter& filter, Result* result) {
  auto fd = ReadOnlyFile::Open(repopath, try_mmap_);
  if (fd == nullptr) {
    if (errno != ENOENT) {
      std::cerr << std::format("failed to open {} for reading: {}\n", repopath,
                               strerror(errno));
    }
    return;
  }

  const char* err;
  const auto read_archive = ReadArchive::New(*fd, &err);
  if (read_archive == nullptr) {
    std::cerr << std::format(
        "failed to create new archive for reading: {}: {}\n", repopath, err);
    return;
  }

  ArchiveReader reader(read_archive->read_archive());

  archive_entry* e;
  while (reader.Next(&e) == ARCHIVE_OK) {
    const char* entryname = archive_entry_pathname(e);

    Package pkg;
    if (!ParsePkgname(&pkg, entryname)) {
      std::cerr << std::format("error parsing pkgname from: {}\n", entryname);
      continue;
    }

    if (entry_callback_(reponame, filter, pkg, result, &reader) < 0) {
      break;
    }
  }
}

int Pkgfile::SearchSingleRepo(const Database& db, const filter::Filter& filter,
                              std::string_view searchstring) {
  std::string wanted_repo;
  if (!options_.targetrepo.empty()) {
    wanted_repo = options_.targetrepo;
  } else {
    wanted_repo = searchstring.substr(0, searchstring.find('/'));
  }

  return SearchRepos(db.GetRepoChunks(wanted_repo), filter);
}

int Pkgfile::SearchRepos(Database::RepoChunks repo_chunks,
                         const filter::Filter& filter) {
  using ResultMap = std::map<std::string, std::unique_ptr<Result>>;
  ResultMap results;

  struct WorkItem {
    const std::string* reponame;
    const std::string* filepath;
    const filter::Filter* filter;
    Result* result;
  };

  ThreadSafeQueue<WorkItem> queue;
  for (auto& [reponame, filepath] : repo_chunks) {
    results.emplace(reponame, std::make_unique<Result>(reponame));
    queue.enqueue(
        WorkItem{&reponame, &filepath, &filter, results[reponame].get()});
  }

  const auto num_workers =
      std::min<int>(std::thread::hardware_concurrency(), queue.size());

  std::vector<std::thread> workers;
  workers.reserve(num_workers);
  for (int i = 0; i < num_workers; ++i) {
    workers.push_back(std::thread([&] {
      while (!queue.empty()) {
        WorkItem item = queue.dequeue();
        ProcessRepo(*item.reponame, *item.filepath, *item.filter, item.result);
      }
    }));
  }

  for (auto& worker : workers) {
    worker.join();
  }

  for (auto iter = results.begin(); iter != results.end();) {
    if (iter->second->Empty()) {
      results.erase(iter++);
    } else {
      ++iter;
    }
  }

  if (results.empty()) {
    return 1;
  }

  const size_t prefixlen =
      options_.raw ? 0
                   : std::max_element(results.begin(), results.end(),
                                      [](const ResultMap::value_type& a,
                                         const ResultMap::value_type& b) {
                                        return a.second->MaxPrefixlen() <
                                               b.second->MaxPrefixlen();
                                      })
                         ->second->MaxPrefixlen();

  for (auto& [repo, result] : results) {
    result->Print(prefixlen, options_.eol);
  }

  return 0;
}

std::unique_ptr<filter::Filter> Pkgfile::BuildFilterFromOptions(
    const Pkgfile::Options& options, const std::string& match) {
  std::unique_ptr<filter::Filter> filter;

  switch (options.filterby) {
    case FilterStyle::EXACT:
      if (options.mode == MODE_SEARCH) {
        if (match.find('/') != match.npos) {
          filter =
              std::make_unique<filter::Exact>(match, options.case_sensitive);
        } else {
          filter =
              std::make_unique<filter::Basename>(match, options.case_sensitive);
        }
      } else if (options.mode == MODE_LIST) {
        auto pos = match.find('/');
        if (pos != match.npos) {
          filter = std::make_unique<filter::Exact>(match.substr(pos + 1),
                                                   options.case_sensitive);
        } else {
          filter =
              std::make_unique<filter::Exact>(match, options.case_sensitive);
        }
      }
      break;
    case FilterStyle::GLOB:
      filter = std::make_unique<filter::Glob>(match, options.case_sensitive);
      break;
    case FilterStyle::REGEX:
      filter = filter::Regex::Compile(match, options.case_sensitive);
      if (filter == nullptr) {
        return nullptr;
      }
      break;
  }

  if (options.mode == MODE_SEARCH) {
    if (options.binaries) {
      filter = std::make_unique<filter::And>(
          std::make_unique<filter::Bin>(bins_), std::move(filter));
    }

    if (!options.directories) {
      filter = std::make_unique<filter::And>(
          std::make_unique<filter::Not>(std::make_unique<filter::Directory>()),
          std::move(filter));
    }
  }

  return filter;
}

int Pkgfile::Run(const std::vector<std::string>& args) {
  if (options_.mode & MODE_UPDATE) {
    return Updater(options_.cachedir, options_.compress,
                   options_.repo_chunk_bytes)
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

  const std::string& input = args[0];

  const auto filter = BuildFilterFromOptions(options_, input);
  if (filter == nullptr) {
    return 1;
  }

  const auto is_repo_package_syntax = [](std::string_view input) {
    auto pos = input.find('/');

    // Make sure we reject anything that starts with a slash.
    return pos != input.npos && pos > 0;
  };

  // override behavior on $repo/$pkg syntax or --repo
  if ((options_.mode == MODE_LIST && is_repo_package_syntax(input)) ||
      !options_.targetrepo.empty()) {
    return SearchSingleRepo(*database, *filter, input);
  }

  return SearchRepos(database->GetAllRepoChunks(), *filter);
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
      " Downloading:\n"
      "  -z, --compress[=type]   compress downloaded repos\n\n";
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
    { "repochunkbytes", required_argument,  0, '~' + 1 },  // undocumented
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
        if (optarg != nullptr) {
          auto compress = pkgfile::ValidateCompression(optarg);
          if (compress == std::nullopt) {
            std::cerr << std::format("error: invalid compression option {}\n", optarg);
            return std::nullopt;
          }
          options.compress = compress.value();
        } else {
          options.compress = ARCHIVE_FILTER_GZIP;
        }
        break;
      case '~' + 1:
        options.repo_chunk_bytes = atoi(optarg);
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
