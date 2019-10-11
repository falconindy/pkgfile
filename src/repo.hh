#pragma once

#include <curl/curl.h>

#include <chrono>
#include <future>
#include <string>
#include <vector>

enum download_Result {
  RESULT_UNKNOWN,
  RESULT_OK,
  RESULT_UPTODATE,
  RESULT_ERROR,
};

struct repo_t {
  explicit repo_t(std::string name) : name(std::move(name)) {}
  ~repo_t();

  repo_t(const repo_t&) = delete;
  repo_t& operator=(const repo_t&) = delete;

  repo_t(repo_t&&) = default;
  repo_t& operator=(repo_t&&) = default;

  std::string name;
  std::vector<std::string> servers;

  int fd;
  std::string arch;

  const struct config_t* config;

  // download stuff, should be moved to a separate class

  // curl easy handle
  CURL* curl = nullptr;
  // destination
  char diskfile[PATH_MAX];
  // iterator to currently in-use server
  decltype(servers)::iterator server_iter;
  // error buffer
  char errmsg[CURL_ERROR_SIZE];
  // numeric err for determining success
  enum download_Result dl_result = RESULT_UNKNOWN;
  // force update repos
  short force = false;
  // start time for download
  std::chrono::time_point<std::chrono::system_clock> dl_time_start;

  std::future<int> worker;

  struct {
    int fd = -1;
    off_t size;
  } tmpfile;
};

struct AlpmConfig {
  AlpmConfig() {}

  static int LoadFromFile(const char* filename, AlpmConfig* config);

  std::vector<repo_t> repos;
  std::string architecture;
};

// vim: set ts=2 sw=2 et:
