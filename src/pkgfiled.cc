#include <getopt.h>
#include <systemd/sd-event.h>
#include <utime.h>

#include <chrono>
#include <format>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "archive_converter.hh"
#include "archive_io.hh"

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

    // If true, skip mtime comparisons between the watch path and pkgfile cache,
    // transcoding all repos found in the watch path.
    bool force = false;

    // If true, synchronize the watch path with the pkgfile cache and then
    // exit.
    bool oneshot = false;
  };

  Pkgfiled(std::string_view watch_path, std::string_view pkgfile_cache,
           Options options)
      : watch_path_(watch_path),
        pkgfile_cache_(pkgfile_cache),
        options_(options) {
    const int shutdown_signal = isatty(fileno(stdin)) ? SIGINT : SIGTERM;

    BlockSignals({shutdown_signal, SIGUSR1, SIGUSR2}, &saved_ss_);

    sd_event_default(&sd_event_);

    sd_event_add_inotify(sd_event_, &inotify_source_, watch_path_.c_str(),
                         IN_MOVED_TO, &Pkgfiled::OnInotifyEvent, this);
    sd_event_source_set_priority(inotify_source_, SD_EVENT_PRIORITY_IMPORTANT);

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

      const char* error;
      auto archive = ReadArchive::New(input_repo, &error);
      if (archive == nullptr) {
        std::cerr << "failed to open " << input_repo
                  << " for reading:" << error;
        return false;
      }

      const std::string reponame = changed_path.filename().stem();
      pkgfile::ArchiveConverter converter(reponame, std::move(archive),
                                          pkgfile_cache_ / changed_path, -1);

      return converter.RewriteArchive();
    };

    const auto start_time = std::chrono::system_clock::now();
    const bool ok = repack();
    if (ok) {
      std::chrono::duration<double> dur =
          std::chrono::system_clock::now() - start_time;

      std::cerr << std::format("finished repacking {} ({:.3f})\n",
                               changed_path.filename().string(), dur.count());
    }

    return ok;
  }

  int OnInotifyEvent(const struct inotify_event* event) {
    const fs::path changed_path(event->name);
    if (changed_path.extension() != kFilesExt) {
      return 0;
    }

    RepackRepo(changed_path);

    return 0;
  }

  static int OnInotifyEvent(sd_event_source*, const struct inotify_event* event,
                            void* userdata) {
    return static_cast<Pkgfiled*>(userdata)->OnInotifyEvent(event);
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
  Options options_;

  sd_event* sd_event_;
  sd_event_source* inotify_source_;
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
  std::cout << "  -f, --force             repack all repos on initial sync\n"
               "  -o, --oneshot           exit after initial sync \n"
               "  -h, --help              display this help and exit\n"
               "  -V, --version           display the version and exit\n\n";
}

void Version(void) { std::cout << "pkgfiled v" PACKAGE_VERSION "\n"; }

std::optional<pkgfile::Pkgfiled::Options> ParseOpts(int* argc, char*** argv) {
  static constexpr char kShortOpts[] = "hofV";
  static constexpr struct option kLongOpts[] = {
      // clang-format off
      { "oneshot",    no_argument,        0, 'o' },
      { "help",       no_argument,        0, 'h' },
      { "force",      no_argument,        0, 'f' },
      { "version",    required_argument,  0, 'V' },
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
      case 'o':
        opts.oneshot = true;
        break;
      case 'f':
        opts.force = true;
        break;
      case 'V':
        Version();
        exit(0);
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
