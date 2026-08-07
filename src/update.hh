#pragma once

#include <curl/curl.h>
#include <systemd/sd-event.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "progress_display.hh"
#include "repo.hh"

namespace pkgfile {

enum class DownloadResult {
  UNKNOWN,
  OK,
  UPTODATE,
  ERROR,
};

// Runtime state for downloading and repacking one repo: the curl handle,
// destination tmpfile, retry cursor into repo.servers, and the async repack
// worker. Lives in an Updater's job list for as long as its download and (if
// triggered) repack are in flight.
struct DownloadJob {
  explicit DownloadJob(Repo repo) : repo(std::move(repo)) {}
  ~DownloadJob();

  // Never copied or moved: Updater stores these in a std::list so curl
  // (CURLOPT_PRIVATE) and in-flight std::async repack workers can hold a
  // stable DownloadJob* for the job's whole lifetime.
  DownloadJob(const DownloadJob&) = delete;
  DownloadJob& operator=(const DownloadJob&) = delete;
  DownloadJob(DownloadJob&&) = delete;
  DownloadJob& operator=(DownloadJob&&) = delete;

  // Owned copy (not a reference into the caller's config) so the job's
  // lifetime never depends on the caller keeping an AlpmConfig alive.
  Repo repo;

  std::string arch;
  // force update repos
  bool force = false;

  // curl easy handle
  CURL* curl = nullptr;
  // destination
  std::string diskfile;
  // iterator to currently in-use server
  std::vector<std::string>::const_iterator server_iter;
  // error buffer
  char errmsg[CURL_ERROR_SIZE] = {};
  // numeric err for determining success
  DownloadResult dl_result = DownloadResult::UNKNOWN;
  // start time for download
  std::chrono::time_point<std::chrono::system_clock> dl_time_start;

  // Shared display this repo's row lives in, and which row it is. Not
  // owned; null (and progress_index unused) if progress reporting is off
  // (e.g. stdout isn't a terminal).
  ProgressDisplay* progress = nullptr;
  size_t progress_index = 0;

  std::future<bool> worker;

  struct {
    int fd = -1;
    off_t size = 0;
  } tmpfile;

  // Invoked exactly once, when this job's outcome is final: a download
  // failure that exhausted every mirror, an up-to-date result, or a
  // completed (successful or failed) repack.
  std::function<void(const Repo&, DownloadResult)> on_done;
};

// Downloads pacman `.files` repo databases and repacks them into the
// pkgfile PFDB cache.
//
// Updater never runs an event loop itself: Update() only registers curl
// transfers and repack completions on an sd_event the caller owns, and
// returns immediately. The caller is responsible for pumping that loop
// (e.g. via sd_event_loop()) until the supplied completion callback fires.
// An Updater instance may only ever be driven by one sd_event over its
// lifetime -- the first event passed to Update() is the one it binds to.
class Updater {
 public:
  explicit Updater(std::string cachedir);
  ~Updater();

  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;

  // Loads `alpm_config_file`, downloads+repacks every repo it declares, and
  // invokes `on_done` with the process exit code the old synchronous
  // Update() used to return, once every repo has resolved.
  void Update(sd_event* event, const std::string& alpm_config_file, bool force,
              std::function<void(int result)> on_done);

 private:
  int DownloadQueueRequest(DownloadJob* job);
  bool RepackRepoData(const DownloadJob* job);
  void TidyCacheDir(const std::set<std::string>& known_repos);

  // Removes `job` from the multi handle and frees its curl easy handle, if
  // any. Safe to call more than once.
  void CleanupCurl(DownloadJob* job);
  // Finalizes `job`: cleans up curl, invokes its on_done callback, and
  // erases it from jobs_. `job` must not be touched afterward.
  void FinishJob(DownloadJob* job);

  void HandleDownloadComplete(DownloadJob* job, CURLMsg* msg);
  void DrainCurlMessages();
  void StartRepack(DownloadJob* job);
  void ReapFinishedRepacks();

  // Binds curl's multi handle to `event` via the socket-action interface,
  // and creates the repack-completion eventfd source. A no-op after the
  // first call.
  void EnsureCurlEventSources(sd_event* event);

  static int SocketCallback(CURL* easy, curl_socket_t s, int action,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* multi, long timeout_ms, void* userp);
  static int OnSocketReady(sd_event_source* source, int fd, uint32_t revents,
                           void* userdata);
  static int OnTimerFired(sd_event_source* source, uint64_t usec,
                          void* userdata);
  static int OnRepackEventFd(sd_event_source* source, int fd, uint32_t revents,
                             void* userdata);

  std::string cachedir_;
  CURLM* curl_multi_;
  std::unique_ptr<ProgressDisplay> progress_;

  // The sd_event this Updater is bound to; see EnsureCurlEventSources().
  sd_event* event_ = nullptr;
  sd_event_source* curl_timer_source_ = nullptr;
  // Running-handle count as of the most recent curl_multi_socket_action()
  // call; used only for the "N remaining" progress print, mirroring what
  // curl_multi_perform()'s out-param gave the old blocking loop.
  int curl_running_handles_ = 0;

  // Signalled by repack worker threads (via std::async) so the event loop
  // can reap finished futures without blocking on them.
  int repack_eventfd_ = -1;
  sd_event_source* repack_eventfd_source_ = nullptr;

  // Stable storage: see DownloadJob's move/copy note above.
  std::list<DownloadJob> jobs_;
};

}  // namespace pkgfile

/* vim: set ts=2 sw=2 et: */
