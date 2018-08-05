#pragma once

#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <curl/curl.h>

struct repo_t {
  char *name;
  char **servers;
  int servercount;
  int fd;
  char *arch;

  const struct config_t *config;

  /* download stuff */

  /* curl easy handle */
  CURL *curl;
  /* destination */
  char diskfile[PATH_MAX];
  /* index to currently in-use server */
  int server_idx;
  /* error buffer */
  char errmsg[CURL_ERROR_SIZE];
  /* numeric err for determining success */
  int err;
  /* force update repos */
  short force;
  /* start time for download */
  double dl_time_start;
  /* PID of repo_repack worker */
  pid_t worker;

  struct {
    int fd;
    off_t size;
  } tmpfile;
};

struct repovec_t {
  struct repo_t **repos;
  int size;
  int capacity;
  char *architecture;
};

#define REPOVEC_FOREACH(r, repos) \
  for (int i_ = 0; i_ < repos->size && (r = repos->repos[i_]); ++i_)

struct repo_t *repo_new(const char *reponame);
void repo_free(struct repo_t *repo);
void repos_free(struct repovec_t *repos);
int repo_add_server(struct repo_t *repo, const char *server);
int load_repos_from_file(const char *filename, struct repovec_t **repos);

/* vim: set ts=2 sw=2 et: */
