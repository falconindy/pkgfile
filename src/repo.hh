#pragma once

#include <curl/curl.h>

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class DownloadResult {
  UNKNOWN,
  OK,
  UPTODATE,
  ERROR,
};

struct Repo {
  explicit Repo(std::string name) : name(std::move(name)) {}
  ~Repo();

  Repo(const Repo&) = delete;
  Repo& operator=(const Repo&) = delete;

  Repo(Repo&&) = default;
  Repo& operator=(Repo&&) = default;

  std::string name;
  std::vector<std::string> servers;

  std::string arch;

  // curl easy handle
  CURL* curl = nullptr;
  // destination
  std::string diskfile;
  // iterator to currently in-use server
  decltype(servers)::iterator server_iter;
  // error buffer
  char errmsg[CURL_ERROR_SIZE];
  // numeric err for determining success
  DownloadResult dl_result = DownloadResult::UNKNOWN;
  // force update repos
  short force = false;
  // start time for download
  std::chrono::time_point<std::chrono::system_clock> dl_time_start;

  std::future<bool> worker;

  struct {
    int fd = -1;
    off_t size;
  } tmpfile;
};

struct AlpmConfig {
  AlpmConfig() {}

  static int LoadFromFile(const char* filename, AlpmConfig* config);

  std::vector<Repo> repos;
  std::string architecture;
};

// Returns the repo name a pkgfile cache database file belongs to, e.g.
// "core.files" -> "core", or std::nullopt if the filename doesn't have a
// ".files" suffix.
std::optional<std::string> RepoNameFromCacheFile(std::string_view filename);

// vim: set ts=2 sw=2 et:
