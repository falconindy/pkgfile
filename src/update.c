#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <curl/curl.h>

#include "macro.h"
#include "pkgfile.h"
#include "repo.h"
#include "update.h"

struct archive_conv {
  struct archive *in;
  struct archive *out;
  struct archive_entry *ae;
  const char *reponame;
  char tmpfile[PATH_MAX];
};

#if defined(CLOCK_MONOTONIC) && !defined(CLOCK_MONOTONIC_COARSE)
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif

static double now(void) {
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC_COARSE)
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

static double simple_pow(int base, int exp) {
  double result = 1.0;
  for (; exp > 0; --exp) {
    result *= base;
  }
  return result;
}

static double humanize_size(off_t bytes, const char target_unit, int precision,
                            const char **label) {
  static const char *labels[] = {"B",   "KiB", "MiB", "GiB", "TiB",
                                 "PiB", "EiB", "ZiB", "YiB"};
  static const int unitcount = sizeof(labels) / sizeof(labels[0]);

  double val = (double)bytes;
  int index;

  for (index = 0; index < unitcount - 1; ++index) {
    if (target_unit != '\0' && labels[index][0] == target_unit) {
      break;
    } else if (target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
      break;
    }
    val /= 1024.0;
  }

  if (label) {
    *label = labels[index];
  }

  /* fix FS#27924 so that it doesn't display negative zeroes */
  if (precision >= 0 && val < 0.0 && val > (-0.5 / simple_pow(10, precision))) {
    val = 0.0;
  }

  return val;
}

static char *strreplace(const char *str, const char *needle,
                        const char *replace) {
  const char *p, *q;
  char *newstr, *newp;
  char *list[8];
  int listsz = 0;
  size_t needlesz = strlen(needle), replacesz = strlen(replace);

  if (!str) {
    return NULL;
  }

  for (p = str, q = strstr(p, needle); q; q = strstr(p, needle)) {
    list[listsz++] = (char *)q;
    p = q + needlesz;
  }

  /* no occurences of needle found */
  if (!listsz) {
    return strdup(str);
  }
  /* size of new string = size of old string + "number of occurences of needle"
   * x "size difference between replace and needle" */
  CALLOC(newstr, strlen(str) + 1 + listsz * (replacesz - needlesz),
         sizeof(char), return NULL);

  p = str;
  newp = newstr;
  for (int i = 0; i < listsz; ++i) {
    q = list[i];
    if (q > p) {
      /* add chars between this occurence and last occurence, if any */
      newp = mempcpy(newp, p, (size_t)(q - p));
    }
    newp = mempcpy(newp, replace, replacesz);
    p = q + needlesz;
  }

  if (*p) {
    /* add the rest of 'p' */
    strcpy(newp, p);
  }

  return newstr;
}

static char *prepare_url(const char *url_template, const char *repo,
                         const char *arch) {
  _cleanup_free_ char *save = NULL, *prepared = NULL;
  char *url;

  prepared = save = strreplace(url_template, "$arch", arch);
  if (prepared == NULL) {
    return NULL;
  }

  prepared = strreplace(save, "$repo", repo);
  if (prepared == NULL) {
    return NULL;
  }

  if (asprintf(&url, "%s/%s.files", prepared, repo) == -1) {
    fputs("error: failed to allocate memory\n", stderr);
    url = NULL;
  }

  return url;
}

static int endswith(const char *s, const char *postfix) {
  size_t sl, pl;

  sl = strlen(s);
  pl = strlen(postfix);

  if (pl == 0 || sl < pl) return 0;

  return memcmp(s + sl - pl, postfix, pl) == 0;
}

static int write_cpio_entry(struct archive_conv *conv, const char *entryname) {
  off_t entry_size = archive_entry_size(conv->ae);
  off_t bytes_w = 0;
  size_t alloc_size = entry_size * 1.1;
  struct archive_line_reader reader = {};
  _cleanup_free_ char *entry_data = NULL, *s = NULL, *line = NULL;

  /* be generous */
  MALLOC(entry_data, alloc_size, return -1);
  MALLOC(line, MAX_LINE_SIZE, return -1);

  reader.line.base = line;

  /* discard the first line */
  reader_getline(&reader, conv->in);

  while (reader_getline(&reader, conv->in) == ARCHIVE_OK) {
    /* ensure enough memory */
    if (bytes_w + reader.line.size + 1 > alloc_size) {
      alloc_size *= 1.1;
      entry_data = realloc(entry_data, alloc_size);
    }

    /* do the copy, with a slash prepended */
    entry_data[bytes_w++] = '/';
    memcpy(&entry_data[bytes_w], reader.line.base, reader.line.size);
    bytes_w += reader.line.size;
    entry_data[bytes_w++] = '\n';
  }

  /* adjust the entry size for removing the first line and adding slashes */
  archive_entry_set_size(conv->ae, bytes_w);

  /* store the metadata as simply $pkgname-$pkgver-$pkgrel */
  s = strdup(entryname);
  *(strrchr(s, '/')) = '\0';
  archive_entry_update_pathname_utf8(conv->ae, s);

  if (archive_write_header(conv->out, conv->ae) != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to write entry header: %s/%s: %s\n",
            conv->reponame, archive_entry_pathname(conv->ae), strerror(errno));
    return -errno;
  }

  if (archive_write_data(conv->out, entry_data, bytes_w) != bytes_w) {
    fprintf(stderr, "error: failed to write entry: %s/%s: %s\n", conv->reponame,
            archive_entry_pathname(conv->ae), strerror(errno));
    return -errno;
  }

  return 0;
}

static void archive_conv_close(struct archive_conv *conv) {
  archive_write_close(conv->out);
  archive_write_free(conv->out);
  archive_read_close(conv->in);
  archive_read_free(conv->in);
}

static int archive_conv_open(struct archive_conv *conv,
                             const struct repo_t *repo) {
  int r;

  /* generally, repo files are gzip compressed, but there's no guarantee of
   * this. in order to be compression-agnostic, use libarchive's reader/writer
   * methods. this also gives us an opportunity to rewrite the archive as CPIO,
   * which is marginally faster given our staunch sequential access. */

  conv->reponame = repo->name;
  stpcpy(stpcpy(conv->tmpfile, repo->diskfile), "~");

  conv->in = archive_read_new();
  conv->out = archive_write_new();

  if (conv->in == NULL || conv->out == NULL) {
    fputs("error: failed to allocate memory for archive objects\n", stderr);
    return -ENOMEM;
  }

  archive_read_support_format_tar(conv->in);
  archive_read_support_filter_all(conv->in);
  r = archive_read_open_fd(conv->in, repo->tmpfile.fd, BUFSIZ);
  if (r != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to create archive reader for %s: %s\n",
            repo->name, strerror(archive_errno(conv->in)));
    r = archive_errno(conv->in);
    goto open_error;
  }

  archive_write_set_format_cpio_newc(conv->out);
  archive_write_add_filter(conv->out, repo->config->compress);
  r = archive_write_open_filename(conv->out, conv->tmpfile);
  if (r != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to open file for writing: %s: %s\n",
            conv->tmpfile, strerror(archive_errno(conv->out)));
    r = archive_errno(conv->out);
    goto open_error;
  }

  return 0;

open_error:
  archive_write_free(conv->out);
  archive_read_free(conv->in);

  return -r;
}

static int repack_repo_data(const struct repo_t *repo) {
  struct archive_conv conv = {};
  int r = 0;

  if (archive_conv_open(&conv, repo) < 0) {
    return -1;
  }

  while (archive_read_next_header(conv.in, &conv.ae) == ARCHIVE_OK) {
    const char *entryname = archive_entry_pathname(conv.ae);

    /* ignore everything but the /files metadata */
    if (endswith(entryname, "/files")) {
      r = write_cpio_entry(&conv, entryname);
      if (r < 0) {
        break;
      }
    }
  }

  archive_conv_close(&conv);

  if (r < 0) {
    /* oh noes! */
    if (unlink(conv.tmpfile) < 0 && errno != ENOENT) {
      fprintf(stderr, "error: failed to unlink temporary file: %s: %s\n",
              conv.tmpfile, strerror(errno));
    }
    return -1;
  }

  if (rename(conv.tmpfile, repo->diskfile) != 0) {
    fprintf(stderr, "error: failed to rotate new repo for %s into place: %s\n",
            repo->name, strerror(errno));
    return -1;
  }

  return 0;
}

static int repack_repo_data_async(struct repo_t *repo) {
  repo->worker = fork();

  if (repo->worker < 0) {
    perror("warning: failed to fork new process");

    /* don't just give up, try to repack the repo synchronously */
    return repack_repo_data(repo);
  }

  if (repo->worker == 0) {
    exit(repack_repo_data(repo));
  }

  return 0;
}

static size_t write_handler(void *ptr, size_t size, size_t nmemb, void *data) {
  struct repo_t *repo = data;
  const uint8_t *p = ptr;
  size_t nbytes = size * nmemb;
  ssize_t n = 0;

  while (nbytes > 0) {
    ssize_t k;

    k = write(repo->tmpfile.fd, p, nbytes);
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

static int open_tmpfile(int flags) {
  const char *tmpdir;
  _cleanup_free_ char *p = NULL;
  int fd;

  tmpdir = getenv("TMPDIR");
  if (tmpdir == NULL) {
    tmpdir = "/tmp";
  }

#ifdef O_TMPFILE
  fd = open(tmpdir, flags | O_TMPFILE, S_IRUSR | S_IWUSR);
  if (fd >= 0) {
    return fd;
  }
#endif

  if (asprintf(&p, "%s/pkgfile-tmp-XXXXXX", tmpdir) < 0) {
    return -ENOMEM;
  }

  fd = mkostemp(p, flags);
  if (fd < 0) {
    return -errno;
  }

  /* ignore any (unlikely) error */
  unlink(p);

  return fd;
}

static int download_queue_request(CURLM *multi, struct repo_t *repo) {
  struct stat st;
  _cleanup_free_ char *url = NULL;

  if (repo->curl == NULL) {
    /* it's my first time, be gentle */
    if (repo->servercount == 0) {
      fprintf(stderr, "error: no servers configured for repo %s\n", repo->name);
      return -1;
    }
    repo->curl = curl_easy_init();
    snprintf(repo->diskfile, sizeof(repo->diskfile), CACHEPATH "/%s.files",
             repo->name);
    curl_easy_setopt(repo->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(repo->curl, CURLOPT_WRITEFUNCTION, write_handler);
    curl_easy_setopt(repo->curl, CURLOPT_WRITEDATA, repo);
    curl_easy_setopt(repo->curl, CURLOPT_PRIVATE, repo);
    curl_easy_setopt(repo->curl, CURLOPT_ERRORBUFFER, repo->errmsg);
    curl_easy_setopt(repo->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(repo->curl, CURLOPT_USERAGENT,
                     PACKAGE_NAME "/v" PACKAGE_VERSION);
    repo->tmpfile.fd = open_tmpfile(O_RDWR | O_NONBLOCK);
    if (repo->tmpfile.fd < 0) {
      fprintf(stderr,
              "error: failed to create temporary file for download: %s\n",
              strerror(-repo->tmpfile.fd));
      return -1;
    }
  } else {
    curl_multi_remove_handle(multi, repo->curl);
    lseek(repo->tmpfile.fd, 0, SEEK_SET);
    ftruncate(repo->tmpfile.fd, 0);
    repo->server_idx++;
  }

  if (repo->server_idx >= repo->servercount) {
    fprintf(stderr, "error: failed to update repo: %s\n", repo->name);
    return -1;
  }

  url = prepare_url(repo->servers[repo->server_idx], repo->name, repo->arch);
  if (url == NULL) {
    fputs("error: failed to allocate URL for download\n", stderr);
    return -1;
  }

  curl_easy_setopt(repo->curl, CURLOPT_URL, url);

  if (repo->force == 0 && stat(repo->diskfile, &st) == 0) {
    curl_easy_setopt(repo->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
    curl_easy_setopt(repo->curl, CURLOPT_TIMECONDITION,
                     CURL_TIMECOND_IFMODSINCE);
  }

  repo->dl_time_start = now();
  curl_multi_add_handle(multi, repo->curl);

  return 0;
}

static int print_rate(double xfer, const char *xfer_label, double rate,
                      const char rate_label) {
  /* We will show 1.62M/s, 11.6M/s, but 116K/s and 1116K/s */
  if (rate < 9.995) {
    return printf("%8.1f %3s  %4.2f%c/s", xfer, xfer_label, rate, rate_label);
  } else if (rate < 99.95) {
    return printf("%8.1f %3s  %4.1f%c/s", xfer, xfer_label, rate, rate_label);
  } else {
    return printf("%8.1f %3s  %4.f%c/s", xfer, xfer_label, rate, rate_label);
  }
}

static void print_download_success(struct repo_t *repo, int remaining) {
  const char *rate_label, *xfered_label;
  double rate, xfered_human;
  int width;

  rate = repo->tmpfile.size / (now() - repo->dl_time_start);
  xfered_human = humanize_size(repo->tmpfile.size, '\0', -1, &xfered_label);

  printf("  download complete: %-20s [", repo->name);
  if (fabs(rate - INFINITY) < DBL_EPSILON) {
    width = printf(" [%6.1f %3s  %7s ", xfered_human, xfered_label, "----");
  } else {
    double rate_human = humanize_size(rate, '\0', -1, &rate_label);
    width = print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
  }
  printf(" %*d remaining]\n", 23 - width, remaining);
}

static void print_total_dl_stats(int count, double duration, off_t total_xfer) {
  const char *rate_label, *xfered_label;
  double rate, xfered_human, rate_human;
  int width;

  rate = total_xfer / duration;
  xfered_human = humanize_size(total_xfer, '\0', -1, &xfered_label);
  rate_human = humanize_size(rate, '\0', -1, &rate_label);

  width = printf(":: download complete in %.2fs", duration);
  printf("%*s<", 42 - width, "");
  print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
  printf(" %2d file%c    >\n", count, count == 1 ? ' ' : 's');
}

static int download_check_complete(CURLM *multi, int remaining) {
  int msgs_left;
  CURLMsg *msg;

  msg = curl_multi_info_read(multi, &msgs_left);
  if (msg == NULL) {
    return -1;
  }

  if (msg->msg == CURLMSG_DONE) {
    long uptodate, resp;
    char *effective_url;
    struct repo_t *repo;

    curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &repo);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &uptodate);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);

    if (uptodate) {
      printf("  %s is up to date\n", repo->name);
      repo->err = 1;
      return 0;
    }

    /* was it a success? */
    if (msg->data.result != CURLE_OK || resp >= 400) {
      if (*repo->errmsg) {
        fprintf(stderr, "warning: download failed: %s: %s\n", effective_url,
                repo->errmsg);
      } else {
        fprintf(stderr, "warning: download failed: %s [error %ld]\n",
                effective_url, resp);
      }

      return download_queue_request(multi, repo);
    }

    repo->tmpfile.size = lseek(repo->tmpfile.fd, 0, SEEK_CUR);
    lseek(repo->tmpfile.fd, 0, SEEK_SET);

    print_download_success(repo, remaining);
    repack_repo_data_async(repo);
    repo->err = 0;
  }

  return 0;
}

static void download_wait_loop(CURLM *multi) {
  int active_handles;

  do {
    int nfd, rc = curl_multi_wait(multi, NULL, 0, 1000, &nfd);
    if (rc != CURLM_OK) {
      fprintf(stderr, "error: curl_multi_wait failed (%d)\n", rc);
      break;
    }

    if (nfd < 0) {
      fprintf(stderr, "error: poll error, possible network problem\n");
      break;
    }

    rc = curl_multi_perform(multi, &active_handles);
    if (rc != CURLM_OK) {
      fprintf(stderr, "error: curl_multi_perform failed (%d)\n", rc);
      break;
    }

    while (download_check_complete(multi, active_handles) == 0)
      ;
  } while (active_handles > 0);
}

static int reap_children(struct repovec_t *repos) {
  int r = 0, running = 0;
  struct repo_t *repo;

  /* immediately reap zombies, but don't wait on still active children */
  REPOVEC_FOREACH(repo, repos) {
    int stat_loc;
    if (repo->worker > 0) {
      if (wait4(repo->worker, &stat_loc, WNOHANG, NULL) == 0) {
        running++;
      } else {
        /* exited, grab the status */
        r += WEXITSTATUS(stat_loc);
      }
    }
  }

  if (running > 0) {
    int stat_loc;
    printf(":: waiting for %d process%s to finish repacking repos...\n",
           running, running == 1 ? "" : "es");
    for (;;) {
      pid_t pid = wait4(-1, &stat_loc, 0, NULL);
      if (pid < 0 && errno == ECHILD) {
        /* no more children */
        break;
      } else if (pid > 0) {
        r += WEXITSTATUS(stat_loc);
      }
    }
  }

  return r;
}

int pkgfile_update(struct repovec_t *repos, struct config_t *config) {
  int r, xfer_count = 0, ret = 0;
  struct repo_t *repo;
  CURLM *curl_multi;
  off_t total_xfer = 0;
  double t_start, duration;

  if (access(CACHEPATH, W_OK)) {
    fprintf(stderr, "error: unable to write to " CACHEPATH ": %s\n",
            strerror(errno));
    return 1;
  }

  printf(":: Updating %d repos...\n", repos->size);

  curl_global_init(CURL_GLOBAL_ALL);
  curl_multi = curl_multi_init();
  if (curl_multi == NULL) {
    /* this can only fail due to out an OOM condition */
    fprintf(stderr, "error: failed to initialize curl: %s\n", strerror(ENOMEM));
    return 1;
  }

  if (repos->architecture == NULL) {
    struct utsname un;
    uname(&un);
    repos->architecture = strdup(un.machine);
  }

  /* ensure all our DBs are 0644 */
  umask(0022);

  /* prime the handle by adding a URL from each repo */
  REPOVEC_FOREACH(repo, repos) {
    repo->arch = repos->architecture;
    repo->force = config->doupdate > 1;
    repo->config = config;
    r = download_queue_request(curl_multi, repo);
    if (r != 0) {
      ret = r;
    }
  }

  t_start = now();
  download_wait_loop(curl_multi);
  duration = now() - t_start;

  /* remove handles, aggregate results */
  REPOVEC_FOREACH(repo, repos) {
    curl_multi_remove_handle(curl_multi, repo->curl);
    curl_easy_cleanup(repo->curl);

    total_xfer += repo->tmpfile.size;

    switch (repo->err) {
      case 0:
        xfer_count++;
        break;
      case -1:
        ret = 1;
        break;
    }
  }

  /* print transfer stats if we downloaded more than 1 file */
  if (xfer_count > 0) {
    print_total_dl_stats(xfer_count, duration, total_xfer);
  }

  if (reap_children(repos) != 0) {
    ret = 1;
  }

  curl_multi_cleanup(curl_multi);
  curl_global_cleanup();

  return ret;
}

/* vim: set ts=2 sw=2 et: */
