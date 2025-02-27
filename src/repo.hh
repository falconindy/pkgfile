#pragma once

#include <curl/curl.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

#include "cista.h"

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

// Verifies that the given file path has a format of "${reponame}.files.nnn"
// where 'n' is an 0-indexed, zero-padded, increasing integer.
bool FilenameHasRepoSuffix(std::string_view path);

// vim: set ts=2 sw=2 et:
