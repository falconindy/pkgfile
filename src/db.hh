#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pkgfile {

// grow this into a more generic pkgfile-wide error category
class DatabaseError : public std::error_category {
 public:
  enum ErrorCondition {
    OK = 0,
    VERSION_FILE_NOT_FOUND,
    WRONG_VERSION,
  };

  const char* name() const noexcept override {
    return "pkgfile::DatabaseError";
  }

  std::string message(int condition) const override;

  static const DatabaseError& Get() {
    static const auto* kCat = new DatabaseError;
    return *kCat;
  }

  static bool Is(std::error_code& ec) { return ec.category() == Get(); }
};

class Database {
 public:
  static std::unique_ptr<Database> Open(std::string_view dbpath,
                                        std::error_code& ec);

  static bool FilenameHasRepoSuffix(std::string_view path);

  static bool WriteDatabaseVersion(std::string_view dbpath);

  struct Entry {
    std::string reponame;
    std::string filename;

    auto operator<=>(const Entry&) const = default;
  };

  using RepoChunks = std::span<const Entry>;

  RepoChunks GetRepoChunks(std::string_view reponame) const;
  RepoChunks GetAllRepoChunks() const;

  bool empty() const { return repos_.empty(); }

 private:
  static constexpr int kVersion = 0;
  static constexpr std::string_view kVersionFilename = ".db_version";

  using Repos = std::vector<Entry>;

  Database(Repos repos) : repos_(std::move(repos)) {}

  Repos repos_;
};

}  // namespace pkgfile
