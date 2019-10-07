#pragma once

#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <curl/curl.h>

#include <future>
#include <string>
#include <vector>

enum download_result_t {
  RESULT_UNKNOWN,
  RESULT_OK,
  RESULT_UPTODATE,
  RESULT_ERROR,
};

struct repo_t {
  explicit repo_t(std::string name) : name(std::move(name)) {}
  ~repo_t();

  repo_t(const repo_t &) = delete;
  repo_t &operator=(const repo_t &) = delete;

  repo_t(repo_t &&) = default;
  repo_t &operator=(repo_t &&) = default;

  std::string name;
  std::vector<std::string> servers;

  int fd;
  const char *arch;

  const struct config_t *config;

  /* download stuff */

  /* curl easy handle */
  CURL *curl = nullptr;
  /* destination */
  char diskfile[PATH_MAX];
  /* index to currently in-use server */
  size_t server_idx = 0;
  /* error buffer */
  char errmsg[CURL_ERROR_SIZE];
  /* numeric err for determining success */
  enum download_result_t dl_result = RESULT_UNKNOWN;
  /* force update repos */
  short force = false;
  /* start time for download */
  double dl_time_start;

  std::future<int> worker;

  struct {
    int fd = -1;
    off_t size;
  } tmpfile;
};

struct repovec_t {
  repovec_t() {}

  std::vector<repo_t> repos;
  std::string architecture;
};

int load_repos_from_file(const char *filename, struct repovec_t *repos);

/* vim: set ts=2 sw=2 et: */
