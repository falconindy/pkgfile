#include "pkgfile.hh"

#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <string.h>

#include <future>
#include <optional>
#include <sstream>
#include <vector>

#include "archive_io.hh"
#include "compress.hh"
#include "filter.hh"
#include "result.hh"
#include "update.hh"

namespace fs = std::filesystem;

namespace pkgfile {

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
}

std::string Pkgfile::FormatSearchResult(const std::string& repo,
                                        const Package& pkg) {
  std::stringstream ss;

  if (options_.verbose) {
    ss << repo << '/' << pkg.name << ' ' << pkg.version;
    return ss.str();
  }

  if (options_.quiet) {
    return std::string(pkg.name);
  }

  ss << repo << '/' << pkg.name;
  return ss.str();
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

  const pkgfile::filter::Bin is_bin;
  std::string_view line;
  while (reader->GetLine(&line) == ARCHIVE_OK) {
    if (options_.binaries && !is_bin.Matches(line)) {
      continue;
    }

    std::string out;
    if (options_.quiet) {
      out.assign(line);
    } else {
      std::stringstream ss;
      ss << repo << '/' << pkg.name;
      out = ss.str();
    }
    result->Add(out, options_.quiet ? std::string() : std::string(line));
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

std::optional<pkgfile::Result> Pkgfile::ProcessRepo(
    const fs::path repo, const filter::Filter& filter) {
  auto fd = ReadOnlyFile::Open(repo, try_mmap_);
  if (fd == nullptr) {
    if (errno != ENOENT) {
      fprintf(stderr, "failed to open %s for reading: %s\n", repo.c_str(),
              strerror(errno));
    }
    return std::nullopt;
  }

  const char* err;
  const auto read_archive = ReadArchive::New(*fd, &err);
  if (read_archive == nullptr) {
    fprintf(stderr, "failed to create new archive for reading: %s: %s\n",
            repo.c_str(), err);
    return std::nullopt;
  }

  Result result(repo.stem());
  ArchiveReader reader(read_archive->read_archive());

  archive_entry* e;
  while (reader.Next(&e) == ARCHIVE_OK) {
    const char* entryname = archive_entry_pathname(e);

    Package pkg;
    if (!ParsePkgname(&pkg, entryname)) {
      fprintf(stderr, "error parsing pkgname from: %s\n", entryname);
      continue;
    }

    if (entry_callback_(repo.stem(), filter, pkg, &result, &reader) < 0) {
      break;
    }
  }

  return result;
}

int Pkgfile::SearchSingleRepo(const RepoMap& repos,
                              const filter::Filter& filter,
                              std::string_view searchstring) {
  std::string wanted_repo;
  if (!options_.targetrepo.empty()) {
    wanted_repo = options_.targetrepo;
  } else {
    wanted_repo = searchstring.substr(0, searchstring.find('/'));
  }

  auto iter = repos.find(wanted_repo);
  if (iter == repos.end()) {
    fprintf(stderr, "error: repo not available: %s\n", wanted_repo.c_str());
  }

  auto result = ProcessRepo(iter->second, filter);
  if (!result.has_value() || result->Empty()) {
    return 1;
  }

  result->Print(options_.raw ? 0 : result->MaxPrefixlen(), options_.eol);
  return 0;
}

int Pkgfile::SearchAllRepos(const RepoMap& repos,
                            const filter::Filter& filter) {
  std::vector<std::future<std::optional<Result>>> futures;
  for (const auto& repo : repos) {
    futures.push_back(std::async(
        std::launch::async, [&] { return ProcessRepo(repo.second, filter); }));
  }

  std::vector<Result> results;
  for (auto& fu : futures) {
    auto result = fu.get();
    if (result.has_value() && !result->Empty()) {
      results.emplace_back(std::move(result.value()));
    }
  }

  if (results.empty()) {
    return 1;
  }

  const size_t prefixlen = options_.raw ? 0 : MaxPrefixlen(results);
  for (auto& result : results) {
    result.Print(prefixlen, options_.eol);
  }

  return 0;
}

// static
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
      filter = std::make_unique<filter::And>(std::make_unique<filter::Bin>(),
                                             std::move(filter));
    }

    if (!options.directories) {
      filter = std::make_unique<filter::And>(
          std::make_unique<filter::Not>(std::make_unique<filter::Directory>()),
          std::move(filter));
    }
  }

  return filter;
}

// static
Pkgfile::RepoMap Pkgfile::DiscoverRepos(std::string_view cachedir,
                                        std::error_code& ec) {
  RepoMap repos;

  for (const auto& p : fs::directory_iterator(cachedir, ec)) {
    if (!p.is_regular_file() || p.path().extension() != ".files") {
      continue;
    }

    repos.emplace(p.path().stem(), p.path());
  }

  return repos;
}

int Pkgfile::Run(const std::vector<std::string>& args) {
  if (options_.mode & MODE_UPDATE) {
    return Updater(options_.cachedir, options_.compress)
        .Update(options_.cfgfile, options_.mode == MODE_UPDATE_FORCE);
  }

  if (args.empty()) {
    fputs("error: no target specified (use -h for help)\n", stderr);
    return 1;
  }

  std::error_code ec;
  const auto repos = DiscoverRepos(options_.cachedir, ec);
  if (ec.value() != 0) {
    fprintf(stderr, "error: Failed to open cache directory %s: %s\n",
            options_.cachedir.c_str(), ec.message().c_str());
  } else if (repos.empty()) {
    fputs("error: No repo files found. Please run `pkgfile --update.\n",
          stderr);
  }

  const std::string& input = args[0];

  const auto filter = Pkgfile::BuildFilterFromOptions(options_, input);
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
    return SearchSingleRepo(repos, *filter, input);
  }

  return SearchAllRepos(repos, *filter);
}

}  // namespace pkgfile

namespace {

void Usage(void) {
  fputs("pkgfile " PACKAGE_VERSION "\nUsage: pkgfile [options] target\n\n",
        stdout);
  fputs(
      " Operations:\n"
      "  -l, --list              list contents of a package\n"
      "  -s, --search            search for packages containing the target "
      "(default)\n"
      "  -u, --update            update repo files lists\n\n",
      stdout);
  fputs(
      " Matching:\n"
      "  -b, --binaries          return only files contained in a bin dir\n"
      "  -d, --directories       match directories in searches\n"
      "  -g, --glob              enable matching with glob characters\n"
      "  -i, --ignorecase        use case insensitive matching\n"
      "  -R, --repo <repo>       search a singular repo\n"
      "  -r, --regex             enable matching with regular expressions\n\n",
      stdout);
  fputs(
      " Output:\n"
      "  -q, --quiet             output less when listing\n"
      "  -v, --verbose           output more\n"
      "  -w, --raw               disable output justification\n"
      "  -0, --null              null terminate output\n\n",
      stdout);
  fputs(
      " Downloading:\n"
      "  -z, --compress[=type]   compress downloaded repos\n\n",
      stdout);
  fputs(
      " General:\n"
      "  -C, --config <file>     use an alternate config (default: "
      "/etc/pacman.conf)\n"
      "  -D, --cachedir <dir>    use an alternate cachedir "
      "(default: " DEFAULT_CACHEPATH
      ")\n"
      "  -h, --help              display this help and exit\n"
      "  -V, --version           display the version and exit\n\n",
      stdout);
}

void Version(void) { fputs(PACKAGE_NAME " v" PACKAGE_VERSION "\n", stdout); }

std::optional<pkgfile::Pkgfile::Options> ParseOpts(int* argc, char*** argv) {
  static constexpr char kShortOpts[] = "0bC:D:dghilqR:rsuVvwz::";
  static constexpr struct option kLongOpts[] = {
      // clang-format off
    { "binaries",      no_argument,        0, 'b' },
    { "cachedir",      required_argument,  0, 'D' },
    { "compress",      optional_argument,  0, 'z' },
    { "config",        required_argument,  0, 'C' },
    { "directories",   no_argument,        0, 'd' },
    { "glob",          no_argument,        0, 'g' },
    { "help",          no_argument,        0, 'h' },
    { "ignorecase",    no_argument,        0, 'i' },
    { "list",          no_argument,        0, 'l' },
    { "quiet",         no_argument,        0, 'q' },
    { "repo",          required_argument,  0, 'R' },
    { "regex",         no_argument,        0, 'r' },
    { "search",        no_argument,        0, 's' },
    { "update",        no_argument,        0, 'u' },
    { "version",       no_argument,        0, 'V' },
    { "verbose",       no_argument,        0, 'v' },
    { "raw",           no_argument,        0, 'w' },
    { "null",          no_argument,        0, '0' },
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
            fprintf(stderr, "error: invalid compression option %s\n", optarg);
            return std::nullopt;
          }
          options.compress = compress.value();
        } else {
          options.compress = ARCHIVE_FILTER_GZIP;
        }
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
