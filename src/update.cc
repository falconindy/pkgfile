#include "update.hh"

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <filesystem>
#include <format>
#include <iostream>

#include "db.hh"
#include "db_builder.hh"
#include "repo.hh"

namespace chrono = std::chrono;
namespace fs = std::filesystem;

auto now = chrono::system_clock::now;

namespace {

std::pair<double, const char*> Humanize(off_t bytes) {
  static constexpr std::array labels{"B",   "KiB", "MiB", "GiB", "TiB",
                                     "PiB", "EiB", "ZiB", "YiB"};

  double val = static_cast<double>(bytes);

  decltype(labels)::const_iterator iter;
  for (iter = labels.begin(); iter != labels.end(); ++iter) {
    if (val <= 2048.0 && val >= -2048.0) {
      break;
    }
    val /= 1024.0;
  }

  if (iter == labels.end()) {
    iter = &labels.back();
  }

  return {val, *iter};
}

void StrReplace(std::string* str, const std::string& needle,
                const std::string& replace) {
  for (;;) {
    auto pos = str->find(needle);
    if (pos == str->npos) {
      break;
    }

    str->replace(pos, needle.length(), replace);
  }
}

std::string PrepareUrl(const std::string& url_template, const std::string& repo,
                       const std::string& arch) {
  std::string url = url_template;

  StrReplace(&url, "$arch", arch);
  StrReplace(&url, "$repo", repo);

  return std::format("{}/{}.files", url, repo);
}

size_t WriteHandler(void* ptr, size_t size, size_t nmemb, void* data) {
  pkgfile::DownloadJob* job = (pkgfile::DownloadJob*)data;
  const uint8_t* p = (uint8_t*)ptr;
  size_t nbytes = size * nmemb;
  ssize_t n = 0;

  while (nbytes > 0) {
    ssize_t k;

    k = write(job->tmpfile.fd, p, nbytes);
    if (k < 0 && errno == EINTR) {
      continue;
    }

    if (k <= 0) {
      return n > 0 ? n : (k < 0 ? -errno : 0);
    }

    p += k;
    nbytes -= k;
    n += k;
  }

  return n;
}

int OpenTmpfile(int flags) {
  const char* tmpdir;
  int fd;

  tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr) {
    tmpdir = "/tmp";
  }

#ifdef O_TMPFILE
  fd = open(tmpdir, flags | O_TMPFILE, S_IRUSR | S_IWUSR);
  if (fd >= 0) {
    return fd;
  }
#endif

  std::string p(tmpdir);
  p.append("/pkgfile-tmp-XXXXXX");

  fd = mkostemp(p.data(), flags);
  if (fd < 0) {
    return -errno;
  }

  // ignore any (unlikely) error
  unlink(p.c_str());

  return fd;
}

int PrintRate(double xfer, const char* xfer_label, double rate,
              const char rate_label) {
  // We will show 1.62M/s, 11.6M/s, but 116K/s and 1116K/s
  if (rate < 9.995) {
    return printf("%8.1f %3s  %4.2f%c/s", xfer, xfer_label, rate, rate_label);
  } else if (rate < 99.95) {
    return printf("%8.1f %3s  %4.1f%c/s", xfer, xfer_label, rate, rate_label);
  } else {
    return printf("%8.1f %3s  %4.f%c/s", xfer, xfer_label, rate, rate_label);
  }
}

void PrintDownloadSuccess(pkgfile::DownloadJob* job, int remaining,
                          double elapsed) {
  double rate = job->tmpfile.size / elapsed;
  auto [xfered_human, xfered_label] = Humanize(job->tmpfile.size);

  printf("  download complete: %-20s [", job->repo.name.c_str());

  int width;
  if (fabs(rate - INFINITY) < DBL_EPSILON) {
    width = printf(" [%6.1f %3s  %7s ", xfered_human, xfered_label, "----");
  } else {
    auto [rate_human, rate_label] = Humanize(rate);
    width = PrintRate(xfered_human, xfered_label, rate_human, rate_label[0]);
  }
  printf(" %*d remaining] (%.2fs)\n", 23 - width, remaining, elapsed);
}

void PrintRepackSuccess(const std::string& reponame, double elapsed) {
  std::cout << std::format("  repack complete: {} ({:.2f}s)\n", reponame,
                           elapsed);
}

// curl's xfer info callback: reports download progress for one repo's
// transfer. Runs on the event loop thread (inside curl_multi_socket_action),
// so no synchronization concerns on this side -- ProgressDisplay handles the
// rest, since repack progress does come from other threads.
int XferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t, curl_off_t) {
  auto* job = static_cast<pkgfile::DownloadJob*>(clientp);
  if (job->progress != nullptr) {
    job->progress->UpdateDownload(job->progress_index, dlnow, dltotal);
  }
  return 0;
}

// Aggregate state for one Update() call, kept alive by the shared_ptr
// captured in each of its jobs' on_done callbacks.
struct UpdateState {
  int remaining;
  int ret = 0;
  std::set<std::string> known_repos;
};

}  // namespace

namespace pkgfile {

DownloadJob::~DownloadJob() {
  if (tmpfile.fd >= 0) {
    close(tmpfile.fd);
  }
}

int Updater::DownloadQueueRequest(DownloadJob* job) {
  if (job->curl == nullptr) {
    if (job->repo.servers.empty()) {
      std::cerr << std::format("error: no servers configured for repo {}\n",
                               job->repo.name);
      return -1;
    }
    job->curl = curl_easy_init();
    job->server_iter = job->repo.servers.begin();

    job->diskfile = cachedir_ + "/" + job->repo.name + ".files";
    curl_easy_setopt(job->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(job->curl, CURLOPT_FILETIME, 1L);
    curl_easy_setopt(job->curl, CURLOPT_WRITEFUNCTION, WriteHandler);
    curl_easy_setopt(job->curl, CURLOPT_WRITEDATA, job);
    curl_easy_setopt(job->curl, CURLOPT_PRIVATE, job);
    curl_easy_setopt(job->curl, CURLOPT_ERRORBUFFER, job->errmsg);
    curl_easy_setopt(job->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(job->curl, CURLOPT_USERAGENT,
                     PACKAGE_NAME "/v" PACKAGE_VERSION);
    curl_easy_setopt(job->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    curl_easy_setopt(job->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(job->curl, CURLOPT_XFERINFOFUNCTION, XferInfoCallback);
    curl_easy_setopt(job->curl, CURLOPT_XFERINFODATA, job);
    job->tmpfile.fd = OpenTmpfile(O_RDWR | O_NONBLOCK);
    if (job->tmpfile.fd < 0) {
      std::cerr << std::format(
          "error: failed to create temporary file for download: {}\n",
          strerror(-job->tmpfile.fd));
      return -1;
    }
  } else {
    curl_multi_remove_handle(curl_multi_, job->curl);
    lseek(job->tmpfile.fd, 0, SEEK_SET);
    job->server_iter++;
  }

  if (job->server_iter == job->repo.servers.end()) {
    std::cerr << std::format("error: failed to update repo: {}\n",
                             job->repo.name);
    return -1;
  }

  std::string url = PrepareUrl(*job->server_iter, job->repo.name, job->arch);

  curl_easy_setopt(job->curl, CURLOPT_URL, url.c_str());

  if (!job->force) {
    // The db file's own mtime is stamped with the upstream archive's mtime
    // when it's (re)written (see DbBuilder::WriteToFile), so a plain stat()
    // here tells us what we last saw from the server.
    struct stat st;
    if (stat(job->diskfile.c_str(), &st) == 0) {
      curl_easy_setopt(job->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
      curl_easy_setopt(job->curl, CURLOPT_TIMECONDITION,
                       CURL_TIMECOND_IFMODSINCE);
    }
  }

  job->dl_time_start = now();
  curl_multi_add_handle(curl_multi_, job->curl);

  return 0;
}

bool Updater::RepackRepoData(const DownloadJob* job) {
  const auto start = now();
  auto FinishRepack = [job, start](ProgressDisplay::Stage stage) {
    const double elapsed = chrono::duration<double>(now() - start).count();
    if (job->progress != nullptr) {
      if (stage == ProgressDisplay::Stage::kDone &&
          !job->progress->IsInteractive()) {
        PrintRepackSuccess(job->repo.name, elapsed);
      }
      job->progress->FinishRepack(job->progress_index, stage, elapsed);
    }
  };

  const char* error;
  auto builder = db::DbBuilder::FromArchive(
      job->repo.name, job->tmpfile.fd, &error, [job](int64_t bytes_read) {
        if (job->progress != nullptr) {
          job->progress->UpdateRepack(job->progress_index, bytes_read,
                                      job->tmpfile.size);
        }
      });
  if (builder == nullptr) {
    std::cerr << std::format("error: failed to read archive for {}: {}\n",
                             job->repo.name, error);
    FinishRepack(ProgressDisplay::Stage::kFailed);
    return false;
  }

  struct stat st;
  if (fstat(job->tmpfile.fd, &st) < 0) {
    std::cerr << std::format(
        "error: failed to stat downloaded archive for {}: {}\n", job->repo.name,
        strerror(errno));
    FinishRepack(ProgressDisplay::Stage::kFailed);
    return false;
  }

  const bool ok = builder->WriteToFile(job->diskfile, st.st_mtim.tv_sec);
  FinishRepack(ok ? ProgressDisplay::Stage::kDone
                  : ProgressDisplay::Stage::kFailed);
  return ok;
}

void Updater::TidyCacheDir(const std::set<std::string>& known_repos) {
  std::error_code ec;

  // For a bit of paranoia, don't try to delete files when we're in a directory
  // that has subdirectories. This should catch the most egregious of cases
  // where someone tries to drop a cachedir in a place that it doesn't belong.
  for (const auto& entry : fs::directory_iterator(cachedir_, ec)) {
    if (entry.is_directory()) {
      std::cerr << "warning: Directory found in pkgfile cachedir. Refusing to "
                   "tidy cachedir.\n";
      return;
    }
  }

  for (const auto& entry : fs::directory_iterator(cachedir_, ec)) {
    const auto reponame =
        RepoNameFromCacheFile(entry.path().filename().native());

    if (!reponame || !known_repos.contains(*reponame)) {
      std::error_code ec;
      fs::remove(entry, ec);
      if (ec.value() != 0) {
        std::cerr << std::format(
            "warning: failed to remove stale cache file: {}\n",
            entry.path().string());
      }
    }
  }
}

void Updater::CleanupCurl(DownloadJob* job) {
  if (job->curl != nullptr) {
    curl_multi_remove_handle(curl_multi_, job->curl);
    curl_easy_cleanup(job->curl);
    job->curl = nullptr;
  }
}

void Updater::FinishJob(DownloadJob* job) {
  CleanupCurl(job);

  auto on_done = std::move(job->on_done);
  Repo repo = std::move(job->repo);
  DownloadResult result = job->dl_result;

  jobs_.remove_if([job](const DownloadJob& j) { return &j == job; });
  // `job` is now dangling; nothing below may touch it.

  if (on_done) {
    on_done(repo, result);
  }
}

void Updater::HandleDownloadComplete(DownloadJob* job, CURLMsg* msg) {
  long uptodate = 0;
  long resp = 0;
  char* effective_url = nullptr;
  time_t remote_mtime = 0;

  curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &uptodate);
  curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);
  curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
  curl_easy_getinfo(msg->easy_handle, CURLINFO_FILETIME_T, &remote_mtime);

  if (uptodate) {
    if (job->progress != nullptr) {
      if (!job->progress->IsInteractive()) {
        std::cout << std::format("  {} is up to date\n", job->repo.name);
      }
      job->progress->FinishDownload(job->progress_index,
                                    ProgressDisplay::Stage::kSkipped);
      job->progress->FinishRepack(job->progress_index,
                                  ProgressDisplay::Stage::kSkipped);
    }
    job->dl_result = DownloadResult::UPTODATE;
    FinishJob(job);
    return;
  }

  // was it a success?
  if (msg->data.result != CURLE_OK || resp >= 400) {
    if (*job->errmsg) {
      std::cerr << std::format("warning: download failed: {}: {}\n",
                               effective_url, job->errmsg);
    } else {
      std::cerr << std::format("warning: download failed: {} [error {}]\n",
                               effective_url, resp);
    }

    if (DownloadQueueRequest(job) != 0) {
      // No more servers left to retry: this repo is done for good.
      job->dl_result = DownloadResult::ERROR;
      if (job->progress != nullptr) {
        job->progress->FinishDownload(job->progress_index,
                                      ProgressDisplay::Stage::kFailed);
        job->progress->FinishRepack(job->progress_index,
                                    ProgressDisplay::Stage::kFailed);
      }
      FinishJob(job);
    }
    return;
  }

  job->tmpfile.size = lseek(job->tmpfile.fd, 0, SEEK_CUR);
  lseek(job->tmpfile.fd, 0, SEEK_SET);

  struct timeval times[2] = {
      {remote_mtime, 0},
      {remote_mtime, 0},
  };
  futimes(job->tmpfile.fd, times);

  if (job->progress != nullptr) {
    const double elapsed =
        chrono::duration<double>(now() - job->dl_time_start).count();
    if (!job->progress->IsInteractive()) {
      PrintDownloadSuccess(job, curl_running_handles_, elapsed);
    }
    job->progress->FinishDownload(job->progress_index,
                                  ProgressDisplay::Stage::kDone, elapsed);
  }

  // The curl transfer is done; release it now rather than waiting for the
  // repack (which may take a while) to finish.
  CleanupCurl(job);
  StartRepack(job);
}

void Updater::DrainCurlMessages() {
  CURLMsg* msg;
  int msgs_left;
  while ((msg = curl_multi_info_read(curl_multi_, &msgs_left)) != nullptr) {
    if (msg->msg != CURLMSG_DONE) {
      continue;
    }
    DownloadJob* job = nullptr;
    curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &job);
    HandleDownloadComplete(job, msg);
  }
}

void Updater::StartRepack(DownloadJob* job) {
  job->worker = std::async(std::launch::async, [this, job] {
    const bool ok = RepackRepoData(job);
    // Wake the event loop so it reaps this job's future without polling.
    // The eventfd counter, not the write count, carries the notification,
    // so a failed write here (ENOMEM-class only; the fd is always valid)
    // just costs a poll-free wakeup we don't get -- nothing to recover.
    uint64_t one = 1;
    (void)!write(repack_eventfd_, &one, sizeof(one));
    return ok;
  });
}

void Updater::ReapFinishedRepacks() {
  // Snapshot which jobs are ready before finishing any of them: FinishJob()
  // erases from jobs_, which would invalidate an in-progress iteration.
  std::vector<DownloadJob*> ready;
  for (auto& job : jobs_) {
    if (job.worker.valid() && job.worker.wait_for(chrono::seconds::zero()) ==
                                  std::future_status::ready) {
      ready.push_back(&job);
    }
  }

  for (DownloadJob* job : ready) {
    const bool repack_ok = job->worker.get();
    job->dl_result = repack_ok ? DownloadResult::OK : DownloadResult::ERROR;
    FinishJob(job);
  }
}

int Updater::SocketCallback(CURL*, curl_socket_t s, int action, void* userp,
                            void* socketp) {
  auto* self = static_cast<Updater*>(userp);
  auto* io_source = static_cast<sd_event_source*>(socketp);

  if (action == CURL_POLL_REMOVE) {
    if (io_source != nullptr) {
      sd_event_source_unref(io_source);
      curl_multi_assign(self->curl_multi_, s, nullptr);
    }
    return 0;
  }

  uint32_t events = 0;
  if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) {
    events |= EPOLLIN;
  }
  if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
    events |= EPOLLOUT;
  }

  if (io_source == nullptr) {
    sd_event_source* source = nullptr;
    sd_event_add_io(self->event_, &source, s, events, &Updater::OnSocketReady,
                    self);
    curl_multi_assign(self->curl_multi_, s, source);
  } else {
    sd_event_source_set_io_events(io_source, events);
  }

  return 0;
}

int Updater::OnSocketReady(sd_event_source*, int fd, uint32_t revents,
                           void* userdata) {
  auto* self = static_cast<Updater*>(userdata);

  int ev_bitmask = 0;
  if (revents & EPOLLIN) {
    ev_bitmask |= CURL_CSELECT_IN;
  }
  if (revents & EPOLLOUT) {
    ev_bitmask |= CURL_CSELECT_OUT;
  }
  if (revents & (EPOLLERR | EPOLLHUP)) {
    ev_bitmask |= CURL_CSELECT_ERR;
  }

  curl_multi_socket_action(self->curl_multi_, fd, ev_bitmask,
                           &self->curl_running_handles_);
  self->DrainCurlMessages();

  return 0;
}

int Updater::TimerCallback(CURLM*, long timeout_ms, void* userp) {
  auto* self = static_cast<Updater*>(userp);

  if (timeout_ms < 0) {
    if (self->curl_timer_source_ != nullptr) {
      sd_event_source_set_enabled(self->curl_timer_source_, SD_EVENT_OFF);
    }
    return 0;
  }

  uint64_t usec_now = 0;
  sd_event_now(self->event_, CLOCK_MONOTONIC, &usec_now);
  const uint64_t deadline = usec_now + static_cast<uint64_t>(timeout_ms) * 1000;

  if (self->curl_timer_source_ == nullptr) {
    sd_event_add_time(self->event_, &self->curl_timer_source_, CLOCK_MONOTONIC,
                      deadline, 0, &Updater::OnTimerFired, self);
  } else {
    sd_event_source_set_time(self->curl_timer_source_, deadline);
  }
  sd_event_source_set_enabled(self->curl_timer_source_, SD_EVENT_ONESHOT);

  return 0;
}

int Updater::OnTimerFired(sd_event_source*, uint64_t, void* userdata) {
  auto* self = static_cast<Updater*>(userdata);
  curl_multi_socket_action(self->curl_multi_, CURL_SOCKET_TIMEOUT, 0,
                           &self->curl_running_handles_);
  self->DrainCurlMessages();
  return 0;
}

int Updater::OnRepackEventFd(sd_event_source*, int fd, uint32_t,
                             void* userdata) {
  auto* self = static_cast<Updater*>(userdata);
  uint64_t count;
  // Just draining the counter to clear readability; the count itself
  // doesn't matter since ReapFinishedRepacks() scans every job.
  (void)!read(fd, &count, sizeof(count));
  self->ReapFinishedRepacks();
  return 0;
}

void Updater::EnsureCurlEventSources(sd_event* event) {
  if (event_ != nullptr) {
    return;
  }
  event_ = event;

  curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETFUNCTION,
                    &Updater::SocketCallback);
  curl_multi_setopt(curl_multi_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(curl_multi_, CURLMOPT_TIMERFUNCTION,
                    &Updater::TimerCallback);
  curl_multi_setopt(curl_multi_, CURLMOPT_TIMERDATA, this);

  repack_eventfd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  sd_event_add_io(event_, &repack_eventfd_source_, repack_eventfd_, EPOLLIN,
                  &Updater::OnRepackEventFd, this);
}

void Updater::Update(sd_event* event, const std::string& alpm_config_file,
                     bool force, std::function<void(int)> on_done) {
  AlpmConfig alpm_config;
  if (AlpmConfig::LoadFromFile(alpm_config_file.c_str(), &alpm_config) < 0) {
    on_done(1);
    return;
  }

  if (alpm_config.repos.empty()) {
    std::cerr << std::format("error: no repos found in {}\n", alpm_config_file);
    on_done(1);
    return;
  }

  if (access(cachedir_.c_str(), W_OK)) {
    std::cerr << std::format("error: unable to write to {}: {}\n", cachedir_,
                             strerror(errno));
    on_done(1);
    return;
  }

  std::cout << std::format(":: Updating {} repos...\n",
                           alpm_config.repos.size());

  if (alpm_config.architecture.empty()) {
    struct utsname un;
    uname(&un);
    alpm_config.architecture = un.machine;
  }

  // ensure all our DBs are 0644
  umask(0022);

  std::vector<std::string> repo_names;
  repo_names.reserve(alpm_config.repos.size());
  for (const auto& repo : alpm_config.repos) {
    repo_names.push_back(repo.name);
  }
  progress_ = std::make_unique<ProgressDisplay>(std::move(repo_names));

  auto state = std::make_shared<UpdateState>();
  state->remaining = static_cast<int>(alpm_config.repos.size());
  for (const auto& repo : alpm_config.repos) {
    state->known_repos.insert(repo.name);
  }

  EnsureCurlEventSources(event);

  size_t i = 0;
  for (const auto& repo : alpm_config.repos) {
    jobs_.emplace_back(repo);
    DownloadJob& job = jobs_.back();
    job.arch = alpm_config.architecture;
    job.force = force;
    job.progress = progress_.get();
    job.progress_index = i++;
    job.on_done = [this, state, on_done](const Repo&, DownloadResult result) {
      if (result == DownloadResult::ERROR) {
        state->ret = 1;
      }
      if (--state->remaining == 0) {
        progress_->Finish();
        TidyCacheDir(state->known_repos);
        if (!Database::WriteDatabaseVersion(cachedir_)) {
          std::cerr << "warning: failed to write database version marker\n";
        }
        on_done(state->ret);
      }
    };

    if (DownloadQueueRequest(&job) != 0) {
      job.dl_result = DownloadResult::ERROR;
      FinishJob(&job);
    }
  }
}

Updater::Updater(std::string cachedir) : cachedir_(std::move(cachedir)) {
  curl_global_init(CURL_GLOBAL_ALL);
  curl_multi_ = curl_multi_init();
}

Updater::~Updater() {
  if (curl_timer_source_ != nullptr) {
    sd_event_source_unref(curl_timer_source_);
  }
  if (repack_eventfd_source_ != nullptr) {
    sd_event_source_unref(repack_eventfd_source_);
  }
  if (repack_eventfd_ >= 0) {
    close(repack_eventfd_);
  }
  curl_multi_cleanup(curl_multi_);
  curl_global_cleanup();
}

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
