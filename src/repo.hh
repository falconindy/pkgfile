#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Static pacman.conf configuration for one repo: its name and the mirror
// URLs it can be fetched from.
struct Repo {
  explicit Repo(std::string name) : name(std::move(name)) {}

  std::string name;
  std::vector<std::string> servers;
};

struct AlpmConfig {
  AlpmConfig() {}

  static int LoadFromFile(const char* filename, AlpmConfig* config);

  std::vector<Repo> repos;
  std::string architecture;
};

// Returns the repo name a pkgfile cache database file belongs to, e.g.
// "core.files" -> "core", or std::nullopt if the filename doesn't have a
// ".files" suffix.
std::optional<std::string> RepoNameFromCacheFile(std::string_view filename);

// vim: set ts=2 sw=2 et:
