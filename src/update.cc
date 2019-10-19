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

bool RepackRepoData(const struct Repo* repo) {
  auto converter = pkgfile::ArchiveConverter::New(
      repo->name, repo->tmpfile.fd, repo->diskfile, repo->config->compress);

  return converter != nullptr && converter->RewriteArchive();
}

size_t WriteHandler(void* ptr, size_t size, size_t nmemb, void* data) {
  struct Repo* repo = (Repo*)data;
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

int OpenTmpfile(int flags) {
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

int DownloadQueueRequest(CURLM* multi, struct Repo* repo) {
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
    curl_easy_setopt(repo->curl, CURLOPT_WRITEFUNCTION, WriteHandler);
    curl_easy_setopt(repo->curl, CURLOPT_WRITEDATA, repo);
    curl_easy_setopt(repo->curl, CURLOPT_PRIVATE, repo);
    curl_easy_setopt(repo->curl, CURLOPT_ERRORBUFFER, repo->errmsg);
    curl_easy_setopt(repo->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(repo->curl, CURLOPT_USERAGENT,
                     PACKAGE_NAME "/v" PACKAGE_VERSION);
    curl_easy_setopt(repo->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    repo->tmpfile.fd = OpenTmpfile(O_RDWR | O_NONBLOCK);
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

int PrintRate(double xfer, const char* xfer_label, double rate,
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

void PrintDownloadSuccess(struct Repo* repo, int remaining) {
  double rate = repo->tmpfile.size /
                chrono::duration<double>(now() - repo->dl_time_start).count();
  auto [xfered_human, xfered_label] = Humanize(repo->tmpfile.size);

  printf("  download complete: %-20s [", repo->name.c_str());

  int width;
  if (fabs(rate - INFINITY) < DBL_EPSILON) {
    width = printf(" [%6.1f %3s  %7s ", xfered_human, xfered_label, "----");
  } else {
    auto [rate_human, rate_label] = Humanize(rate);
    width = PrintRate(xfered_human, xfered_label, rate_human, rate_label[0]);
  }
  printf(" %*d remaining]\n", 23 - width, remaining);
}

void PrintTotalDlStats(int count, double duration, off_t total_xfer) {
  double rate = total_xfer / duration;
  auto [xfered_human, xfered_label] = Humanize(total_xfer);
  auto [rate_human, rate_label] = Humanize(rate);

  int width = printf(":: download complete in %.2fs", duration);
  printf("%*s<", 42 - width, "");
  PrintRate(xfered_human, xfered_label, rate_human, rate_label[0]);
  printf(" %2d file%c    >\n", count, count == 1 ? ' ' : 's');
}

int DownloadCheckComplete(CURLM* multi, int remaining) {
  int msgs_left;

  CURLMsg* msg = curl_multi_info_read(multi, &msgs_left);
  if (msg == nullptr) {
    return -1;
  }

  if (msg->msg == CURLMSG_DONE) {
    long uptodate, resp;
    char* effective_url;
    struct Repo* repo;
    time_t remote_mtime;

    curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &repo);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_CONDITION_UNMET, &uptodate);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &resp);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &effective_url);
    curl_easy_getinfo(msg->easy_handle, CURLINFO_FILETIME_T, &remote_mtime);

    if (uptodate) {
      printf("  %s is up to date\n", repo->name.c_str());
      repo->dl_result = DownloadResult::UPTODATE;
      return 0;
    }

    // was it a success?
    if (msg->data.result != CURLE_OK || resp >= 400) {
      repo->dl_result = DownloadResult::ERROR;
      if (*repo->errmsg) {
        fprintf(stderr, "warning: download failed: %s: %s\n", effective_url,
                repo->errmsg);
      } else {
        fprintf(stderr, "warning: download failed: %s [error %ld]\n",
                effective_url, resp);
      }

      return DownloadQueueRequest(multi, repo);
    }

    repo->tmpfile.size = lseek(repo->tmpfile.fd, 0, SEEK_CUR);
    lseek(repo->tmpfile.fd, 0, SEEK_SET);

    struct timeval times[2] = {
        {remote_mtime, 0},
        {remote_mtime, 0},
    };
    futimes(repo->tmpfile.fd, times);

    PrintDownloadSuccess(repo, remaining);
    repo->worker =
        std::async(std::launch::async, [repo] { return RepackRepoData(repo); });
    repo->dl_result = DownloadResult::OK;
  }

  return 0;
}

void DownloadWaitLoop(CURLM* multi) {
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

    while (DownloadCheckComplete(multi, active_handles) == 0)
      ;
  } while (active_handles > 0);
}

int WaitForRepacking(std::vector<Repo>* repos) {
  int running =
      std::count_if(repos->begin(), repos->end(), [](const Repo& repo) {
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

  return std::count_if(repos->begin(), repos->end(), [](Repo& repo) {
    return repo.worker.valid() && !repo.worker.get();
  });
}

}  // namespace

namespace pkgfile {

int Updater::Update(struct config_t* config) {
  int r, xfer_count = 0, ret = 0;
  off_t total_xfer = 0;

  AlpmConfig alpm_config;
  ret = AlpmConfig::LoadFromFile(config->cfgfile, &alpm_config);
  if (ret < 0) {
    return 1;
  }

  if (alpm_config.repos.empty()) {
    fprintf(stderr, "error: no repos found in %s\n", config->cfgfile);
    return 1;
  }

  if (access(config->cachedir, W_OK)) {
    fprintf(stderr, "error: unable to write to %s: %s\n", config->cachedir,
            strerror(errno));
    return 1;
  }

  printf(":: Updating %zd repos...\n", alpm_config.repos.size());

  if (alpm_config.architecture.empty()) {
    struct utsname un;
    uname(&un);
    alpm_config.architecture = un.machine;
  }

  // ensure all our DBs are 0644
  umask(0022);

  auto& repos = alpm_config.repos;

  // prime the handle by adding a URL from each repo
  for (auto& repo : repos) {
    repo.arch = alpm_config.architecture;
    repo.force = config->mode == MODE_UPDATE_FORCE;
    repo.config = config;
    r = DownloadQueueRequest(curl_multi_, &repo);
    if (r != 0) {
      ret = r;
    }
  }

  auto t_start = now();
  DownloadWaitLoop(curl_multi_);
  chrono::duration<double> dur = now() - t_start;

  // remove handles, aggregate results
  for (auto& repo : repos) {
    curl_multi_remove_handle(curl_multi_, repo.curl);
    curl_easy_cleanup(repo.curl);

    total_xfer += repo.tmpfile.size;

    switch (repo.dl_result) {
      case DownloadResult::OK:
        xfer_count++;
        break;
      case DownloadResult::UPTODATE:
        break;
      case DownloadResult::ERROR:
        ret = 1;
        break;
      default:
        fprintf(stderr, "BUG: unhandled repo->dl_result=%d\n",
                static_cast<int>(repo.dl_result));
        break;
    }
  }

  // print transfer stats if we downloaded more than 1 file
  if (xfer_count > 0) {
    PrintTotalDlStats(xfer_count, dur.count(), total_xfer);
  }

  if (WaitForRepacking(&repos) != 0) {
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
