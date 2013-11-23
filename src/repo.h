/*
 * Copyright (C) 2011-2013 by Dave Reisner <dreisner@archlinux.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <limits.h>
#include <sys/time.h>

#include <curl/curl.h>

struct repo_t {
  char *name;
  char **servers;
  int servercount;
  int filefound;
  char *arch;

  const struct config_t *config;

  /* download stuff */

  /* curl easy handle */
  CURL *curl;
  /* url being fetched */
  char *url;
  /* destination */
  char diskfile[PATH_MAX];
  /* index to currently in-use server */
  int server_idx;
  /* write buffer for downloaded data */
  unsigned char *data;
  /* max capacity of write buffer */
  size_t capacity;
  /* size of data written */
  size_t buflen;
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
};

struct repovec_t {
  struct repo_t **repos;
  int size;
  int capacity;
};

#define REPOVEC_FOREACH(r, repos) \
  for (int i_ = 0; i_ < repos->size && (r = repos->repos[i_]); i_++)

struct repo_t *repo_new(const char *reponame);
void repo_free(struct repo_t *repo);
void repos_free(struct repovec_t *repos);
int repo_add_server(struct repo_t *repo, const char *server);
struct repovec_t *load_repos_from_file(const char *filename);

/* vim: set ts=2 sw=2 et: */
