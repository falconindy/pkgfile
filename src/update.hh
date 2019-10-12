#pragma once

#include <curl/curl.h>

#define PACMANCONFIG "/etc/pacman.conf"

struct config_t;
class AlpmConfig;

namespace pkgfile {

class Updater {
 public:
  Updater();
  ~Updater();

  int Update(AlpmConfig* alpm_config, struct config_t* config);

 private:
  CURLM* curl_multi_;
};

}  // namespace pkgfile

/* vim: set ts=2 sw=2 et: */
