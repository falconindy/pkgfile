#include <getopt.h>
#include <string.h>
#include <systemd/sd-event.h>
#include <utime.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "archive_converter.hh"
#include "compress.hh"
#include "repo.hh"

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kFilesExt = ".files";

void BlockSignals(std::initializer_list<int> signums, sigset_t* saved) {
  sigset_t ss;
  for (auto signum : signums) {
    sigaddset(&ss, signum);
  }

  sigprocmask(SIG_BLOCK, &ss, saved);
}

bool NeedsUpdate(const fs::path& subject, fs::file_time_type mtime) {
  std::error_code ec;
  fs::file_time_type subject_mtime = fs::last_write_time(subject, ec);

  return ec.value() != 0 || subject_mtime < mtime;
}

}  // namespace

namespace pkgfile {

class Pkgfiled {
 public:
  struct Options {
    Options() {}

    int compress = 0;  // ARCHIVE_FILTER_NONE

    // If true, skip mtime comparisons between the watch path and pkgfile cache,
    // transcoding all repos found in the watch path.
    bool force = false;

    // If true, synchronize the watch path with the pkgfile cache and then
    // exit.
    bool oneshot = false;

    // Path to the pacman config file describing which repos are enabled.
    std::string config = DEFAULT_PACMAN_CONF;
  };

  Pkgfiled(std::string_view watch_path, std::string_view pkgfile_cache,
           Options options)
      : watch_path_(watch_path),
        pkgfile_cache_(pkgfile_cache),
        config_path_(options.config),
        options_(options),
        enabled_repos_(ReadEnabledRepos().value_or(std::set<std::string>{})) {
    const int shutdown_signal = isatty(fileno(stdin)) ? SIGINT : SIGTERM;

    BlockSignals({shutdown_signal, SIGUSR1, SIGUSR2}, &saved_ss_);

    sd_event_default(&sd_event_);

    sd_event_add_inotify(sd_event_, &inotify_source_, watch_path_.c_str(),
                         IN_MOVED_TO, &Pkgfiled::OnInotifyEvent, this);
    sd_event_source_set_priority(inotify_source_, SD_EVENT_PRIORITY_IMPORTANT);

    // Watch the directory containing the config file rather than the file
    // itself so that we still notice changes when an editor replaces the file
    // by rename (write-to-temp + rename-over). No IN_CREATE because editors
    // like vim do an open with O_CREAT|O_TRUNC and then rewrite the file. This
    // would trigger the inotify watch twice, with the first time deleting the
    // whole cache.
    sd_event_add_inotify(sd_event_, &config_source_,
                         config_path_.parent_path().c_str(),
                         IN_CLOSE_WRITE | IN_MOVED_TO,
                         &Pkgfiled::OnConfigEvent, this);
    sd_event_source_set_priority(config_source_, SD_EVENT_PRIORITY_IMPORTANT);

    sd_event_add_signal(sd_event_, &sigterm_source_, shutdown_signal,
                        &Pkgfiled::OnSignalEvent, this);
    sd_event_source_set_priority(sigterm_source_, SD_EVENT_PRIORITY_IDLE);

    sd_event_add_signal(sd_event_, &sigusr1_source_, SIGUSR1,
                        &Pkgfiled::OnSignalEvent, this);
    sd_event_add_signal(sd_event_, &sigusr2_source_, SIGUSR2,
                        &Pkgfiled::OnSignalEvent, this);
  }

  ~Pkgfiled() {
    sd_event_source_unref(inotify_source_);
    sd_event_source_unref(config_source_);
    sd_event_source_unref(sigterm_source_);
    sd_event_source_unref(sigusr1_source_);
    sd_event_source_unref(sigusr2_source_);
    sd_event_unref(sd_event_);

    sigprocmask(SIG_SETMASK, &saved_ss_, nullptr);
  }

  int Run() {
    Sync(options_.force);

    if (options_.oneshot) {
      return 0;
    }

    return sd_event_loop(sd_event_);
  }

  int Sync(bool force_update) {
    std::vector<std::future<void>> repack_futures;

    for (auto& p : fs::directory_iterator(watch_path_)) {
      if (!p.is_regular_file() || p.path().extension() != kFilesExt) {
        continue;
      }

      if (!enabled_repos_.contains(p.path().stem().string())) {
        continue;
      }

      if (!force_update && !NeedsUpdate(pkgfile_cache_ / p.path().filename(),
                                        p.last_write_time())) {
        continue;
      }

      repack_futures.emplace_back(std::async(
          std::launch::async, [this, p] { RepackRepo(p.path().filename()); }));
    }

    for (auto& f : repack_futures) {
      f.get();
    }

    return 0;
  }

 private:
  bool RepackRepo(const fs::path& changed_path) {
    auto repack = [&] {
      const std::string input_repo = watch_path_ / changed_path;

      std::cerr << std::format("processing new files DB: {}\n",
                               input_repo.c_str());

      const auto input_file = ReadOnlyFile::Open(input_repo, /*try_mmap=*/true);
      if (input_file == nullptr) {
        return false;
      }

      const std::string reponame = changed_path.filename().stem();
      auto converter = pkgfile::ArchiveConverter::New(
          reponame, input_file->fd(), pkgfile_cache_ / changed_path,
          options_.compress, -1);

      return converter != nullptr && converter->RewriteArchive();
    };

    const auto start_time = std::chrono::system_clock::now();
    const bool ok = repack();
    if (ok) {
      std::chrono::duration<double> dur =
          std::chrono::system_clock::now() - start_time;

      std::cerr << std::format("finished repacking {} ({:.3f}s)\n",
                               changed_path.filename().string(), dur.count());
    }

    return ok;
  }

  // Reads the set of enabled repo names from the pacman config file, or
  // std::nullopt if the config could not be parsed. Callers keep their current
  // set on failure, so a malformed config (e.g. an editor mid-save) can never
  // wipe the cache.
  std::optional<std::set<std::string>> ReadEnabledRepos() {
    AlpmConfig alpm_config;
    if (AlpmConfig::LoadFromFile(config_path_.c_str(), &alpm_config) < 0) {
      std::cerr << std::format("warning: failed to parse {}\n",
                               config_path_.string());
      return std::nullopt;
    }

    std::set<std::string> repos;
    for (auto& repo : alpm_config.repos) {
      repos.insert(std::move(repo.name));
    }

    return repos;
  }

  // Removes the pkgfile cache DBs belonging to any of the given repos. Unlike
  // Updater::TidyCacheDir, this only touches files owned by the named repos,
  // leaving the rest of the cache (e.g. the .db_version marker) untouched.
  void RemoveReposFromCache(const std::set<std::string>& repos) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(pkgfile_cache_, ec)) {
      const auto reponame =
          RepoNameFromCacheFile(entry.path().filename().native());
      if (!reponame || !repos.contains(*reponame)) {
        continue;
      }

      fs::remove(entry, ec);
      if (ec.value() != 0) {
        std::cerr << std::format(
            "warning: failed to remove cache file {}: {}\n",
            entry.path().string(), ec.message());
      } else {
        std::cerr << std::format("removed cache for disabled repo: {}\n",
                                 entry.path().filename().string());
      }
    }
  }

  int OnInotifyEvent(const struct inotify_event* event) {
    const fs::path changed_path(event->name);
    if (changed_path.extension() != kFilesExt) {
      return 0;
    }

    if (!enabled_repos_.contains(changed_path.stem().string())) {
      return 0;
    }

    RepackRepo(changed_path);

    return 0;
  }

  static int OnInotifyEvent(sd_event_source*, const struct inotify_event* event,
                            void* userdata) {
    return static_cast<Pkgfiled*>(userdata)->OnInotifyEvent(event);
  }

  int OnConfigEvent(const struct inotify_event* event) {
    if (config_path_.filename() != event->name) {
      return 0;
    }

    auto parsed = ReadEnabledRepos();
    if (!parsed.has_value() || *parsed == enabled_repos_) {
      return 0;
    }
    std::set<std::string>& next = *parsed;

    std::vector<std::string> added;
    std::set<std::string> removed;
    std::set_difference(next.begin(), next.end(), enabled_repos_.begin(),
                        enabled_repos_.end(), std::back_inserter(added));
    std::set_difference(enabled_repos_.begin(), enabled_repos_.end(),
                        next.begin(), next.end(),
                        std::inserter(removed, removed.end()));

    enabled_repos_ = std::move(next);

    std::cerr << std::format(
        "{} changed: {} repo(s) added, {} repo(s) removed\n",
        config_path_.string(), added.size(), removed.size());

    RemoveReposFromCache(removed);

    // Catch up on newly-enabled repos whose .files already exist in the watch
    // path; future updates arrive via the normal inotify path.
    for (const auto& reponame : added) {
      const fs::path input = reponame + std::string(kFilesExt);
      std::error_code ec;
      if (fs::exists(watch_path_ / input, ec)) {
        RepackRepo(input);
      }
    }

    return 0;
  }

  static int OnConfigEvent(sd_event_source*, const struct inotify_event* event,
                           void* userdata) {
    return static_cast<Pkgfiled*>(userdata)->OnConfigEvent(event);
  }

  int OnSignalEvent(const struct signalfd_siginfo* si) {
    switch (si->ssi_signo) {
      case SIGTERM:
      case SIGINT:
        std::cerr << std::format("{} received, shutting down\n",
                                 strsignal(si->ssi_signo));
        sd_event_exit(sd_event_, 0);
        break;
      case SIGUSR1:
      case SIGUSR2:
        bool force = si->ssi_signo == SIGUSR2;
        std::cerr << std::format("{} received, repacking repos (force={})\n",
                                 strsignal(si->ssi_signo),
                                 force ? "true" : "false");
        Sync(force);
        break;
    }

    return 0;
  }

  static int OnSignalEvent(sd_event_source*, const struct signalfd_siginfo* si,
                           void* userdata) {
    return static_cast<Pkgfiled*>(userdata)->OnSignalEvent(si);
  }

  fs::path watch_path_;
  fs::path pkgfile_cache_;
  fs::path config_path_;
  Options options_;
  std::set<std::string> enabled_repos_;

  sd_event* sd_event_;
  sd_event_source* inotify_source_;
  sd_event_source* config_source_;
  sd_event_source* sigterm_source_;
  sd_event_source* sigusr1_source_;
  sd_event_source* sigusr2_source_;
  sigset_t saved_ss_{};
};

}  // namespace pkgfile

namespace {

void Usage() {
  std::cout << "pkgfiled " PACKAGE_VERSION
               "\nUsage: pkgfiled [options] pacman_source pkgfile_dest\n\n";
  std::cout << "  -C, --config <file>     use an alternate pacman config "
               "(default: " DEFAULT_PACMAN_CONF
               ")\n"
               "  -f, --force             repack all repos on initial sync\n"
               "  -o, --oneshot           exit after initial sync \n"
               "  -z, --compress[=type]   compress downloaded repos\n\n"
               "  -h, --help              display this help and exit\n"
               "  -V, --version           display the version and exit\n\n";
}

void Version(void) { std::cout << "pkgfiled v" PACKAGE_VERSION "\n"; }

std::optional<pkgfile::Pkgfiled::Options> ParseOpts(int* argc, char*** argv) {
  static constexpr char kShortOpts[] = "C:hofVz:";
  static constexpr struct option kLongOpts[] = {
      // clang-format off
      { "config",     required_argument,  0, 'C' },
      { "oneshot",    no_argument,        0, 'o' },
      { "help",       no_argument,        0, 'h' },
      { "force",      no_argument,        0, 'f' },
      { "compress",   required_argument,  0, 'z' },
      { "version",    no_argument,        0, 'V' },
      { 0, 0, 0, 0 },
      // clang-format on
  };

  pkgfile::Pkgfiled::Options opts;
  for (;;) {
    int opt = getopt_long(*argc, *argv, kShortOpts, kLongOpts, nullptr);
    if (opt < 0) {
      break;
    }

    switch (opt) {
      case 'h':
        Usage();
        exit(0);
      case 'C':
        opts.config = optarg;
        break;
      case 'o':
        opts.oneshot = true;
        break;
      case 'f':
        opts.force = true;
        break;
      case 'V':
        Version();
        exit(0);
      case 'z':
        if (optarg != nullptr) {
          opts.compress = pkgfile::ValidateCompression(optarg).value_or(-1);
          if (opts.compress < 0) {
            std::cerr << std::format("error: invalid compression option {}\n",
                                     optarg);
            return std::nullopt;
          }
        } else {
          opts.compress = ARCHIVE_FILTER_GZIP;
        }
        break;
      default:
        return std::nullopt;
    }
  }

  *argc -= optind - 1;
  *argv += optind - 1;

  return opts;
}

}  // namespace

int main(int argc, char** argv) {
  auto options = ParseOpts(&argc, &argv);
  if (options == std::nullopt) {
    return 2;
  }

  if (argc < 3) {
    std::cerr << std::format("error: not enough arguments (use -h for help)\n");
    return 1;
  }

  return pkgfile::Pkgfiled(argv[1], argv[2], options.value()).Run();
}
