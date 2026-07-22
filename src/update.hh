#pragma once

#include <curl/curl.h>

#include <chrono>
#include <cstddef>
#include <future>
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

// Runtime state for downloading and repacking one repo, scoped to a single
// Updater::Update() run: the curl handle, destination tmpfile, retry cursor
// into repo.servers, and the async repack worker.
struct DownloadJob {
  explicit DownloadJob(const Repo& repo) : repo(repo) {}
  ~DownloadJob();

  DownloadJob(const DownloadJob&) = delete;
  DownloadJob& operator=(const DownloadJob&) = delete;

  DownloadJob(DownloadJob&&) = default;
  DownloadJob& operator=(DownloadJob&&) = delete;

  const Repo& repo;

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
  char errmsg[CURL_ERROR_SIZE];
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
    off_t size;
  } tmpfile;
};

class Updater {
 public:
  Updater(std::string cachedir);
  ~Updater();

  int Update(const std::string& alpm_config_file, bool force);

 private:
  int DownloadQueueRequest(CURLM* multi, DownloadJob* job);
  void DownloadWaitLoop(CURLM* multi);
  int DownloadCheckComplete(CURLM* multi, int remaining);
  bool RepackRepoData(const DownloadJob* job);
  void TidyCacheDir(const std::set<std::string>& known_repos);

  std::string cachedir_;
  CURLM* curl_multi_;
  std::unique_ptr<ProgressDisplay> progress_;
};

}  // namespace pkgfile

/* vim: set ts=2 sw=2 et: */
