#include "update.hh"

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>

#include "archive_converter.hh"
#include "archive_reader.hh"
#include "pkgfile.hh"
#include "repo.hh"

namespace chrono = std::chrono;
namespace fs = std::filesystem;

auto now = chrono::system_clock::now;

namespace {

std::pair<double, const char*> Humanize(off_t bytes) {
  static constexpr std::array labels{"B",   "KiB", "MiB", "GiB", "TiB",
                                     "PiB", "EiB", "ZiB", "YiB"};

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
    if (pos == str->npos) {
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

  return std::format("{}/{}.files", url, repo);
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
    std::cout << std::format(
        ":: waiting for {} repo{} to finish repacking...\n", running,
        running == 1 ? "" : "s");
  }

  return std::count_if(repos->begin(), repos->end(), [](Repo& repo) {
    return repo.worker.valid() && !repo.worker.get();
  });
}

}  // namespace

namespace pkgfile {

int Updater::DownloadQueueRequest(CURLM* multi, struct Repo* repo) {
  struct stat st;

  if (repo->curl == nullptr) {
    if (repo->servers.empty()) {
      std::cerr << std::format("error: no servers configured for repo {}\n",
                               repo->name);
      return -1;
    }
    repo->curl = curl_easy_init();
    repo->server_iter = repo->servers.begin();

    repo->diskfile = cachedir_ + "/" + repo->name + ".files";
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
      std::cerr << std::format(
          "error: failed to create temporary file for download: {}\n",
          strerror(-repo->tmpfile.fd));
      return -1;
    }
  } else {
    curl_multi_remove_handle(multi, repo->curl);
    lseek(repo->tmpfile.fd, 0, SEEK_SET);
    repo->server_iter++;
  }

  if (repo->server_iter == repo->servers.end()) {
    std::cerr << std::format("error: failed to update repo: %s\n", repo->name);
    return -1;
  }

  std::string url = PrepareUrl(*repo->server_iter, repo->name, repo->arch);

  curl_easy_setopt(repo->curl, CURLOPT_URL, url.c_str());

  std::string first_repo_chunk = std::format("{}.000", repo->diskfile);
  if (repo->force == 0 && stat(first_repo_chunk.c_str(), &st) == 0) {
    curl_easy_setopt(repo->curl, CURLOPT_TIMEVALUE, (long)st.st_mtime);
    curl_easy_setopt(repo->curl, CURLOPT_TIMECONDITION,
                     CURL_TIMECOND_IFMODSINCE);
  }

  repo->dl_time_start = now();
  curl_multi_add_handle(multi, repo->curl);

  return 0;
}

bool Updater::RepackRepoData(const struct Repo* repo) {
  auto converter = pkgfile::ArchiveConverter::New(repo->name, repo->tmpfile.fd,
                                                  repo->diskfile, compress_,
                                                  repo_chunk_bytes_);

  return converter != nullptr && converter->RewriteArchive();
}

void Updater::TidyCacheDir(const std::set<std::string>& known_repos) {
  std::error_code ec;

  // For a bit of paranoia, don't try to delete files when we're in a directory
  // that has subdirectories. This should catch the most egregious of cases
  // where someone tries to drop a cachedir in a place that it doesn't belong.
  for (const auto& entry : fs::directory_iterator(cachedir_, ec)) {
    if (entry.is_directory()) {
      std::cerr << "warning: Directory found in pkgfile cachedir. Refusing to "
                   "tidy cachedir.\n";
      return;
    }
  }

  for (const auto& entry : fs::directory_iterator(cachedir_, ec)) {
    const fs::path filename = entry.path().filename();
    const fs::path reponame = entry.path().stem().stem();

    if (!FilenameHasRepoSuffix(filename.c_str()) ||
        !known_repos.contains(reponame)) {
      std::error_code ec;
      fs::remove(entry, ec);
      if (ec.value() != 0) {
        std::cerr << std::format(
            "warning: failed to remove stale cache file: {}\n",
            entry.path().string());
      }
    }
  }
}

void Updater::DownloadWaitLoop(CURLM* multi) {
  int active_handles;

  do {
    int nfd, rc = curl_multi_wait(multi, nullptr, 0, 1000, &nfd);
    if (rc != CURLM_OK) {
      std::cerr << std::format("error: curl_multi_wait failed ({})\n", rc);
      break;
    }

    if (nfd < 0) {
      std::cerr << "error: poll error, possible network problem\n";
      break;
    }

    rc = curl_multi_perform(multi, &active_handles);
    if (rc != CURLM_OK) {
      std::cerr << std::format("error: curl_multi_perform failed ({})\n", rc);
      break;
    }

    while (DownloadCheckComplete(multi, active_handles) == 0);
  } while (active_handles > 0);
}

int Updater::DownloadCheckComplete(CURLM* multi, int remaining) {
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
      std::cout << std::format("  {} is up to date\n", repo->name);
      repo->dl_result = DownloadResult::UPTODATE;
      return 0;
    }

    // was it a success?
    if (msg->data.result != CURLE_OK || resp >= 400) {
      repo->dl_result = DownloadResult::ERROR;
      if (*repo->errmsg) {
        std::cerr << std::format("warning: download failed: {}: {}\n",
                                 effective_url, repo->errmsg);
      } else {
        std::cerr << std::format("warning: download failed: {} [error {}]\n",
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
    repo->worker = std::async(std::launch::async,
                              [this, repo] { return RepackRepoData(repo); });
    repo->dl_result = DownloadResult::OK;
  }

  return 0;
}

int Updater::Update(const std::string& alpm_config_file, bool force) {
  int r, xfer_count = 0, ret = 0;
  off_t total_xfer = 0;

  AlpmConfig alpm_config;
  ret = AlpmConfig::LoadFromFile(alpm_config_file.c_str(), &alpm_config);
  if (ret < 0) {
    return 1;
  }

  if (alpm_config.repos.empty()) {
    std::cerr << std::format("error: no repos found in {}\n", alpm_config_file);
    return 1;
  }

  if (access(cachedir_.c_str(), W_OK)) {
    std::cerr << std::format("error: unable to write to {}: {}\n", cachedir_,
                             strerror(errno));
    return 1;
  }

  std::cout << std::format(":: Updating {} repos...\n",
                           alpm_config.repos.size());

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
    repo.force = force;
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

  std::set<std::string> known_repos;
  for (const auto& repo : alpm_config.repos) {
    known_repos.insert(repo.name);
  }

  TidyCacheDir(known_repos);

  if (!Database::WriteDatabaseVersion(cachedir_)) {
    std::cerr << "warning: failed to write database version marker\n";
  }

  return ret;
}

Updater::Updater(std::string cachedir, int compress, int repo_chunk_bytes)
    : cachedir_(cachedir),
      compress_(compress),
      repo_chunk_bytes_(repo_chunk_bytes) {
  curl_global_init(CURL_GLOBAL_ALL);
  curl_multi_ = curl_multi_init();
}

Updater::~Updater() {
  curl_multi_cleanup(curl_multi_);
  curl_global_cleanup();
}

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
