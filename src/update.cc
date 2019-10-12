#include "update.hh"

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <sstream>

#include "archive_converter.hh"
#include "archive_reader.hh"
#include "pkgfile.hh"
#include "repo.hh"

namespace chrono = std::chrono;

auto now = chrono::system_clock::now;

namespace {

std::pair<double, const char*> Humanize(off_t bytes) {
  static constexpr std::array<const char*, 9> labels{
      "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"};

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
    if (pos == std::string::npos) {
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

  std::stringstream ss;

  ss << url << '/' << repo << ".files";
  return ss.str();
}

bool repack_repo_data(const struct repo_t* repo) {
  auto converter = pkgfile::ArchiveConverter::New(
      repo->name, repo->tmpfile.fd, repo->diskfile, repo->config->compress);

  return converter != nullptr && converter->RewriteArchive();
}

size_t write_handler(void* ptr, size_t size, size_t nmemb, void* data) {
  struct repo_t* repo = (repo_t*)data;
  const uint8_t* p = (uint8_t*)ptr;
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

int open_tmpfile(int flags) {
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

int download_queue_request(CURLM* multi, struct repo_t* repo) {
  struct stat st;

  if (repo->curl == nullptr) {
    if (repo->servers.empty()) {
      fprintf(stderr, "error: no servers configured for repo %s\n",
              repo->name.c_str());
      return -1;
    }
    repo->curl = curl_easy_init();
    repo->server_iter = repo->servers.begin();

    repo->diskfile =
        std::string(repo->config->cachedir) + "/" + repo->name + ".files";
    curl_easy_setopt(repo->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(repo->curl, CURLOPT_FILETIME, 1L);
    curl_easy_setopt(repo->curl, CURLOPT_WRITEFUNCTION, write_handler);
    curl_easy_setopt(repo->curl, CURLOPT_WRITEDATA, repo);
    curl_easy_setopt(repo->curl, CURLOPT_PRIVATE, repo);
    curl_easy_setopt(repo->curl, CURLOPT_ERRORBUFFER, repo->errmsg);
    curl_easy_setopt(repo->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(repo->curl, CURLOPT_USERAGENT,
                     PACKAGE_NAME "/v" PACKAGE_VERSION);
    curl_easy_setopt(repo->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
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
    repo->server_iter++;
  }

  if (repo->server_iter == repo->servers.end()) {
    fprintf(stderr, "error: failed to update repo: %s\n", repo->name.c_str());
    return -1;
  }

  std::string url = PrepareUrl(*repo->server_iter, repo->name, repo->arch);

  curl_easy_setopt(repo->curl, CURLOPT_URL, url.c_str());

  if (repo->force == 0 && stat(repo->diskfile.c_str(), &st) == 0) {
    curl_easy_setopt(repo->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
    curl_easy_setopt(repo->curl, CURLOPT_TIMECONDITION,
                     CURL_TIMECOND_IFMODSINCE);
  }

  repo->dl_time_start = now();
  curl_multi_add_handle(multi, repo->curl);

  return 0;
}

int print_rate(double xfer, const char* xfer_label, double rate,
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

void print_download_success(struct repo_t* repo, int remaining) {
  double rate = repo->tmpfile.size /
                chrono::duration<double>(now() - repo->dl_time_start).count();
  auto [xfered_human, xfered_label] = Humanize(repo->tmpfile.size);

  printf("  download complete: %-20s [", repo->name.c_str());

  int width;
  if (fabs(rate - INFINITY) < DBL_EPSILON) {
    width = printf(" [%6.1f %3s  %7s ", xfered_human, xfered_label, "----");
  } else {
    auto [rate_human, rate_label] = Humanize(rate);
    width = print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
  }
  printf(" %*d remaining]\n", 23 - width, remaining);
}

void print_total_dl_stats(int count, double duration, off_t total_xfer) {
  double rate = total_xfer / duration;
  auto [xfered_human, xfered_label] = Humanize(total_xfer);
  auto [rate_human, rate_label] = Humanize(rate);

  int width = printf(":: download complete in %.2fs", duration);
  printf("%*s<", 42 - width, "");
  print_rate(xfered_human, xfered_label, rate_human, rate_label[0]);
  printf(" %2d file%c    >\n", count, count == 1 ? ' ' : 's');
}

int download_check_complete(CURLM* multi, int remaining) {
  int msgs_left;

  CURLMsg* msg = curl_multi_info_read(multi, &msgs_left);
  if (msg == nullptr) {
    return -1;
  }

  if (msg->msg == CURLMSG_DONE) {
    long uptodate, resp;
    char* effective_url;
    struct repo_t* repo;
    time_t remote_mtime;

    curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &repo);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &uptodate);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_FILETIME_T, &remote_mtime);

    if (uptodate) {
      printf("  %s is up to date\n", repo->name.c_str());
      repo->dl_result = RESULT_UPTODATE;
      return 0;
    }

    // was it a success?
    if (msg->data.result != CURLE_OK || resp >= 400) {
      repo->dl_result = RESULT_ERROR;
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

    struct timeval times[2] = {
        {remote_mtime, 0},
        {remote_mtime, 0},
    };
    futimes(repo->tmpfile.fd, times);

    print_download_success(repo, remaining);
    repo->worker = std::async(std::launch::async,
                              [repo] { return repack_repo_data(repo); });
    repo->dl_result = RESULT_OK;
  }

  return 0;
}

void download_wait_loop(CURLM* multi) {
  int active_handles;

  do {
    int nfd, rc = curl_multi_wait(multi, nullptr, 0, 1000, &nfd);
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

int wait_for_repacking(std::vector<repo_t>* repos) {
  int running =
      std::count_if(repos->begin(), repos->end(), [](const repo_t& repo) {
        // The future won't be valid if the repo was up to date.
        if (!repo.worker.valid()) {
          return false;
        }

        return repo.worker.wait_for(chrono::seconds::zero()) !=
               std::future_status::ready;
      });

  if (running > 0) {
    printf(":: waiting for %d repo%s to finish repacking...\n", running,
           running == 1 ? "" : "s");
  }

  return std::count_if(repos->begin(), repos->end(), [](repo_t& repo) {
    return repo.worker.valid() && !repo.worker.get();
  });
}

}  // namespace

namespace pkgfile {

int Updater::Update(AlpmConfig* alpm_config, struct config_t* config) {
  int r, xfer_count = 0, ret = 0;
  off_t total_xfer = 0;

  if (access(config->cachedir, W_OK)) {
    fprintf(stderr, "error: unable to write to %s: %s\n", config->cachedir,
            strerror(errno));
    return 1;
  }

  printf(":: Updating %zd repos...\n", alpm_config->repos.size());

  if (alpm_config->architecture.empty()) {
    struct utsname un;
    uname(&un);
    alpm_config->architecture = un.machine;
  }

  // ensure all our DBs are 0644
  umask(0022);

  auto& repos = alpm_config->repos;

  // prime the handle by adding a URL from each repo
  for (auto& repo : repos) {
    repo.arch = alpm_config->architecture;
    repo.force = config->doupdate > 1;
    repo.config = config;
    r = download_queue_request(curl_multi_, &repo);
    if (r != 0) {
      ret = r;
    }
  }

  auto t_start = now();
  download_wait_loop(curl_multi_);
  chrono::duration<double> dur = now() - t_start;

  // remove handles, aggregate results
  for (auto& repo : repos) {
    curl_multi_remove_handle(curl_multi_, repo.curl);
    curl_easy_cleanup(repo.curl);

    total_xfer += repo.tmpfile.size;

    switch (repo.dl_result) {
      case RESULT_OK:
        xfer_count++;
        break;
      case RESULT_UPTODATE:
        break;
      case RESULT_ERROR:
        ret = 1;
        break;
      default:
        fprintf(stderr, "BUG: unhandled repo->dl_result=%d\n", repo.dl_result);
        break;
    }
  }

  // print transfer stats if we downloaded more than 1 file
  if (xfer_count > 0) {
    print_total_dl_stats(xfer_count, dur.count(), total_xfer);
  }

  if (wait_for_repacking(&repos) != 0) {
    ret = 1;
  }

  return ret;
}

Updater::Updater() {
  curl_global_init(CURL_GLOBAL_ALL);
  curl_multi_ = curl_multi_init();
}

Updater::~Updater() {
  curl_multi_cleanup(curl_multi_);
  curl_global_cleanup();
}

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
