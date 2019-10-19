#pragma once

#include <curl/curl.h>

struct config_t;
class AlpmConfig;

namespace pkgfile {

class Updater {
 public:
  Updater();
  ~Updater();

  int Update(struct config_t* config);

 private:
  CURLM* curl_multi_;
};

}  // namespace pkgfile

/* vim: set ts=2 sw=2 et: */
