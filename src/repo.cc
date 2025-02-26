#include "repo.hh"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

std::pair<std::string_view, std::string_view> split_keyval(
    std::string_view line, const char separator) {
  if (auto pos = line.find(separator); pos != line.npos) {
    return {line.substr(0, pos), line.substr(pos + 1)};
  }

  return {line, std::string_view()};
}

int parse_one_file(const char*, std::string*, AlpmConfig*);

int parse_include(std::string_view include, std::string* section,
                  AlpmConfig* alpm_config) {
  glob_t globbuf;

  const std::string include_str(include);
  if (glob(include_str.c_str(), GLOB_NOCHECK, nullptr, &globbuf) != 0) {
    fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
            include_str.c_str());
    return -ENOMEM;
  }

  for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
    parse_one_file(globbuf.gl_pathv[i], section, alpm_config);
  }

  globfree(&globbuf);

  return 0;
}

std::string_view trim(std::string_view in) {
  auto left = in.begin();

  for (;; ++left) {
    if (left == in.end()) {
      return std::string_view();
    }
    if (!isspace(*left)) {
      break;
    }
  }

  auto right = in.end() - 1;
  for (; right > left && isspace(*right); --right);

  return std::string_view(left, std::distance(left, right) + 1);
}

int parse_one_file(const char* filename, std::string* section,
                   AlpmConfig* alpm_config) {
  FILE* fp;
  char buffer[4096];
  static constexpr std::string_view kServer = "Server";
  static constexpr std::string_view kInclude = "Include";
  static constexpr std::string_view kArchitecture = "Architecture";
  static constexpr std::string_view kAuto = "auto";
  int in_options = 0, r = 0, lineno = 0;

  fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "error: failed to open '%s': %s\n", filename,
            strerror(errno));
    return -errno;
  }

  while (fgets(buffer, sizeof(buffer), fp)) {
    ++lineno;

    std::string_view line(buffer, sizeof(buffer));
    line = line.substr(0, line.find('\0'));

    // Remove comments
    if (auto pos = line.find('#'); pos != line.npos) {
      line = line.substr(0, pos);
    }

    line = trim(line);
    if (line.empty()) {
      continue;
    }

    // found a section header
    if (line.front() == '[' && line.back() == ']') {
      section->assign(line.substr(1, line.size() - 2));
      in_options = *section == "options";
      if (!in_options) {
        alpm_config->repos.emplace_back(*section);
      }
    }

    if (auto pos = line.find('='); pos != line.npos) {
      auto [key, value] = split_keyval(line, '=');

      key = trim(key);
      value = trim(value);

      if (key == kServer) {
        if (section->empty()) {
          fprintf(
              stderr,
              "error: failed to parse %s on line %d: found 'Server' directive "
              "outside of a section\n",
              filename, lineno);
          continue;
        }
        if (in_options) {
          fprintf(
              stderr,
              "error: failed to parse %s on line %d: found 'Server' directive "
              "in options section\n",
              filename, lineno);
          continue;
        }

        alpm_config->repos.back().servers.emplace_back(value.data(),
                                                       value.size());
      } else if (key == kInclude) {
        parse_include(value, section, alpm_config);
      } else if (in_options && key == kArchitecture) {
        if (value != kAuto) {
          // More recent pacman allows alternative architectures, space
          // delimited. In this case, take only the first value.
          auto [arch, rest] = split_keyval(value, ' ');

          alpm_config->architecture.assign(arch);
        }
      }
    }
  }

  fclose(fp);

  return r;
}

}  // namespace

int AlpmConfig::LoadFromFile(const char* filename, AlpmConfig* alpm_config) {
  std::string section;
  int k;

  k = parse_one_file(filename, &section, alpm_config);
  if (k < 0) {
    return k;
  }

  return 0;
}

Repo::~Repo() {
  if (tmpfile.fd >= 0) {
    close(tmpfile.fd);
  }
}

bool FilenameHasRepoSuffix(std::string_view path) {
  const auto ndots = std::count(path.begin(), path.end(), '.');
  if (ndots != 2) {
    return false;
  }

  auto pos = path.rfind('.');
  if (!path.substr(0, pos).ends_with(".files")) {
    return false;
  }

  int ndigits = 0;
  for (++pos; pos < path.size(); ++pos) {
    if (!isdigit(path[pos])) {
      return false;
    }
    ++ndigits;
  }

  return ndigits == 3;
}

// vim: set ts=2 sw=2 et:
