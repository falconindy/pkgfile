#include "pkgfile.hh"

#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <string.h>
#include <sys/mman.h>

#include <future>
#include <sstream>
#include <vector>

#include "compress.hh"
#include "filter.hh"
#include "repo.hh"
#include "result.hh"
#include "update.hh"

static struct config_t config;

static const char* filtermethods[2] = {"glob", "regex"};

static std::string format_search_result(const std::string& repo,
                                        const Package& pkg) {
  std::stringstream ss;

  if (config.verbose) {
    ss << repo << '/' << pkg.name << ' ' << pkg.version;
    return ss.str();
  }

  if (config.quiet) {
    return std::string(pkg.name);
  }

  ss << repo << '/' << pkg.name;
  return ss.str();
}

static int search_metafile(const std::string& repo,
                           const pkgfile::filter::Filter& filter,
                           const Package& pkg, pkgfile::Result* result,
                           pkgfile::ArchiveReader* reader) {
  std::string line;
  while (reader->GetLine(&line) == ARCHIVE_OK) {
    if (!filter.Matches(line)) {
      continue;
    }

    result->Add(format_search_result(repo, pkg),
                config.verbose ? line : std::string());

    if (!config.verbose) {
      return 0;
    }
  }

  return 0;
}

static int list_metafile(const std::string& repo,
                         const pkgfile::filter::Filter& filter,
                         const Package& pkg, pkgfile::Result* result,
                         pkgfile::ArchiveReader* reader) {
  if (!filter.Matches(pkg.name)) {
    return 0;
  }

  pkgfile::filter::Bin is_bin;
  std::string line;
  while (reader->GetLine(&line) == ARCHIVE_OK) {
    if (config.binaries && !is_bin.Matches(line)) {
      continue;
    }

    std::string out;
    if (config.quiet) {
      out.assign(line);
    } else {
      std::stringstream ss;
      ss << repo << '/' << pkg.name;
      out = ss.str();
    }
    result->Add(out, config.quiet ? std::string() : line);
  }

  // When we encounter a match with fixed string matching, we know we're done.
  // However, for other filter methods, we can't be sure that our pattern won't
  // produce further matches, so we signal our caller to continue.
  return config.filterby == FILTER_EXACT ? -1 : 0;
}

static int parse_pkgname(Package* pkg, std::string_view entryname) {
  pkg->name = entryname;

  // handle errors
  pkg->name.remove_suffix(pkg->name.size() - pkg->name.rfind('-'));
  pkg->name.remove_suffix(pkg->name.size() - pkg->name.rfind('-'));

  pkg->version = entryname.substr(pkg->name.size() + 1);

  return 0;
}

static pkgfile::Result load_repo(repo_t* repo,
                                 const pkgfile::filter::Filter& filter) {
  char repofile[FILENAME_MAX];
  std::string line;
  archive* a;
  archive_entry* e;
  struct stat st;
  void* repodata = MAP_FAILED;

  pkgfile::Result result(repo->name);

  snprintf(repofile, sizeof(repofile), "%s/%s.files", config.cachedir,
           repo->name.c_str());

  a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

  repo->fd = open(repofile, O_RDONLY);
  if (repo->fd < 0) {
    // fail silently if the file doesn't exist
    if (errno != ENOENT) {
      fprintf(stderr, "error: failed to open repo: %s: %s\n", repofile,
              strerror(errno));
    }
    // goto cleanup;
  }

  fstat(repo->fd, &st);
  repodata =
      mmap(0, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, repo->fd, 0);
  if (repodata == MAP_FAILED) {
    fprintf(stderr, "error: failed to map pages for %s: %s\n", repofile,
            strerror(errno));
    return result;
    // goto cleanup;
  }

  if (archive_read_open_memory(a, repodata, st.st_size) != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to load repo: %s: %s\n", repofile,
            archive_error_string(a));
    return result;
    // goto cleanup;
  }

  pkgfile::ArchiveReader reader(a);

  while (reader.Next(&e) == ARCHIVE_OK) {
    const char* entryname = archive_entry_pathname(e);

    Package pkg;
    int r = parse_pkgname(&pkg, entryname);
    if (r < 0) {
      fprintf(stderr, "error parsing pkgname from: %s: %s\n", entryname,
              strerror(-r));
      continue;
    }

    r = config.filefunc(repo->name, filter, pkg, &result, &reader);
    if (r < 0) {
      break;
    }
  }

  archive_read_close(a);

  // cleanup:
  archive_read_free(a);
  if (repo->fd >= 0) {
    close(repo->fd);
  }
  if (repodata != MAP_FAILED) {
    munmap(repodata, st.st_size);
  }

  return result;
}

static void usage(void) {
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

static void print_version(void) {
  fputs(PACKAGE_NAME " v" PACKAGE_VERSION "\n", stdout);
}

static int parse_opts(int argc, char** argv) {
  static constexpr char kPacmanConfig[] = "/etc/pacman.conf";
  static constexpr char kShortOpts[] = "0bC:D:dghilqR:rsuVvwz::";
  static constexpr struct option kLongOpts[] = {
      {"binaries", no_argument, 0, 'b'},
      {"cachedir", required_argument, 0, 'D'},
      {"compress", optional_argument, 0, 'z'},
      {"config", required_argument, 0, 'C'},
      {"directories", no_argument, 0, 'd'},
      {"glob", no_argument, 0, 'g'},
      {"help", no_argument, 0, 'h'},
      {"ignorecase", no_argument, 0, 'i'},
      {"list", no_argument, 0, 'l'},
      {"quiet", no_argument, 0, 'q'},
      {"repo", required_argument, 0, 'R'},
      {"regex", no_argument, 0, 'r'},
      {"search", no_argument, 0, 's'},
      {"update", no_argument, 0, 'u'},
      {"version", no_argument, 0, 'V'},
      {"verbose", no_argument, 0, 'v'},
      {"raw", no_argument, 0, 'w'},
      {"null", no_argument, 0, '0'},
      {0, 0, 0, 0}};

  // defaults
  config.filefunc = search_metafile;
  config.eol = '\n';
  config.cfgfile = kPacmanConfig;
  config.cachedir = DEFAULT_CACHEPATH;
  config.mode = MODE_SEARCH;

  for (;;) {
    int opt = getopt_long(argc, argv, kShortOpts, kLongOpts, nullptr);
    if (opt < 0) {
      break;
    }
    switch (opt) {
      case '0':
        config.eol = '\0';
        break;
      case 'b':
        config.binaries = true;
        break;
      case 'C':
        config.cfgfile = optarg;
        break;
      case 'D':
        config.cachedir = optarg;
        break;
      case 'd':
        config.directories = true;
        break;
      case 'g':
        if (config.filterby != FILTER_EXACT) {
          fprintf(stderr, "error: --glob cannot be used with --%s option\n",
                  filtermethods[config.filterby]);
          return 1;
        }
        config.filterby = FILTER_GLOB;
        break;
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case 'i':
        config.icase = true;
        break;
      case 'l':
        config.mode = MODE_LIST;
        config.filefunc = list_metafile;
        break;
      case 'q':
        config.quiet = true;
        break;
      case 'R':
        config.targetrepo = optarg;
        break;
      case 'r':
        if (config.filterby != FILTER_EXACT) {
          fprintf(stderr, "error: --regex cannot be used with --%s option\n",
                  filtermethods[config.filterby]);
          return 1;
        }
        config.filterby = FILTER_REGEX;
        break;
      case 's':
        config.mode = MODE_SEARCH;
        config.filefunc = search_metafile;
        break;
      case 'u':
        if (config.mode & MODE_UPDATE) {
          config.mode = MODE_UPDATE_FORCE;
        } else {
          config.mode = MODE_UPDATE_ASNEEDED;
        }
        break;
      case 'V':
        print_version();
        exit(EXIT_SUCCESS);
      case 'v':
        config.verbose = true;
        break;
      case 'w':
        config.raw = true;
        break;
      case 'z':
        if (optarg != nullptr) {
          auto compress = pkgfile::ValidateCompression(optarg);
          if (compress == std::nullopt) {
            fprintf(stderr, "error: invalid compression option %s\n", optarg);
            return 1;
          }
          config.compress = compress.value();
        } else {
          config.compress = ARCHIVE_FILTER_GZIP;
        }
        break;
      default:
        return 1;
    }
  }

  return 0;
}

static int search_single_repo(std::vector<repo_t>* repos,
                              const pkgfile::filter::Filter& filter,
                              std::string_view searchstring) {
  std::string_view wanted_repo;
  if (config.targetrepo) {
    wanted_repo = config.targetrepo;
  } else {
    wanted_repo = searchstring.substr(0, searchstring.find('/'));
  }

  for (auto& repo : *repos) {
    if (repo.name != wanted_repo) {
      continue;
    }

    auto result = load_repo(&repo, filter);
    result.Print(config.raw ? 0 : result.MaxPrefixlen(), config.eol);

    return result.Empty();
  }

  // repo not found
  fprintf(stderr, "error: repo not available: %s\n", config.targetrepo);

  return 1;
}

static std::vector<pkgfile::Result> search_all_repos(
    std::vector<repo_t>* repos, const pkgfile::filter::Filter& filter) {
  std::vector<pkgfile::Result> results;
  std::vector<std::future<pkgfile::Result>> futures;

  for (auto& repo : *repos) {
    futures.push_back(std::async(std::launch::async,
                                 [&] { return load_repo(&repo, filter); }));
  }

  for (auto& fu : futures) {
    results.emplace_back(fu.get());
  }

  return results;
}

std::unique_ptr<pkgfile::filter::Filter> BuildFilterFromOptions(
    const config_t& config, const std::string& match) {
  std::unique_ptr<pkgfile::filter::Filter> filter;

  bool case_sensitive = !config.icase;

  switch (config.filterby) {
    case FILTER_EXACT:
      if (config.mode == MODE_SEARCH) {
        if (match.find('/') != std::string::npos) {
          filter =
              std::make_unique<pkgfile::filter::Exact>(match, case_sensitive);
        } else {
          filter = std::make_unique<pkgfile::filter::Basename>(match,
                                                               case_sensitive);
        }
      } else if (config.mode == MODE_LIST) {
        auto pos = match.find('/');
        if (pos != std::string::npos) {
          filter = std::make_unique<pkgfile::filter::Exact>(
              match.substr(pos + 1), case_sensitive);
        } else {
          filter =
              std::make_unique<pkgfile::filter::Exact>(match, case_sensitive);
        }
      }
      break;
    case FILTER_GLOB:
      filter = std::make_unique<pkgfile::filter::Glob>(match, case_sensitive);
      break;
    case FILTER_REGEX:
      filter = pkgfile::filter::Regex::Compile(match, case_sensitive);
      if (filter == nullptr) {
        return nullptr;
      }
      break;
  }

  if (config.mode == MODE_SEARCH) {
    if (config.binaries) {
      filter = std::make_unique<pkgfile::filter::And>(
          std::make_unique<pkgfile::filter::Bin>(), std::move(filter));
    }

    std::unique_ptr<pkgfile::filter::Filter> dir_filter =
        std::make_unique<pkgfile::filter::Directory>();
    if (!config.directories) {
      dir_filter =
          std::make_unique<pkgfile::filter::Not>(std::move(dir_filter));
    }

    filter = std::make_unique<pkgfile::filter::And>(std::move(dir_filter),
                                                    std::move(filter));
  }

  return filter;
}

int main(int argc, char* argv[]) {
  int reposfound = 0, ret = 0;
  AlpmConfig alpm_config;
  std::vector<pkgfile::Result> results;

  setlocale(LC_ALL, "");

  if (parse_opts(argc, argv) != 0) {
    return 2;
  }

  ret = AlpmConfig::LoadFromFile(config.cfgfile, &alpm_config);
  if (ret < 0) {
    return 1;
  }

  auto* repos = &alpm_config.repos;
  if (repos->empty()) {
    fprintf(stderr, "error: no repos found in %s\n", config.cfgfile);
    return 1;
  }

  if (config.mode & MODE_UPDATE) {
    pkgfile::Updater updater;
    return !!updater.Update(&alpm_config, &config);
  }

  if (optind == argc) {
    fputs("error: no target specified (use -h for help)\n", stderr);
    return 1;
  }

  auto filter = BuildFilterFromOptions(config, argv[optind]);
  if (filter == nullptr) {
    return 1;
  }

  // override behavior on $repo/$pkg syntax or --repo
  if ((config.mode == MODE_LIST && strchr(argv[optind], '/')) ||
      config.targetrepo) {
    ret = search_single_repo(repos, *filter, argv[optind]);
  } else {
    results = search_all_repos(&alpm_config.repos, *filter);

    size_t prefixlen = config.raw ? 0 : MaxPrefixlen(results);
    for (size_t i = 0; i < alpm_config.repos.size(); ++i) {
      reposfound += alpm_config.repos[i].fd >= 0;
      ret += (int)results[i].Print(prefixlen, config.eol);
    }

    if (!reposfound) {
      fputs("error: No repo files found. Please run `pkgfile --update'.\n",
            stderr);
    }

    ret = ret > 0 ? 0 : 1;
  }

  return ret;
}

// vim: set ts=2 sw=2 et:
