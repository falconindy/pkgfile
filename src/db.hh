#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mapped_repo.hh"

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

// Database is the set of pkgfile-format repo files (one mmap'd MappedRepo
// per repo) found in a cachedir.
class Database {
 public:
  static std::unique_ptr<Database> Open(std::string_view dbpath,
                                        std::error_code& ec);

  static bool WriteDatabaseVersion(std::string_view dbpath);

  // Returns the repo named `reponame`, or nullptr if the cachedir has no such
  // repo.
  const db::MappedRepo* GetRepo(std::string_view reponame) const;

  std::span<const std::unique_ptr<db::MappedRepo>> GetAllRepos() const {
    return repos_;
  }

  bool empty() const { return repos_.empty(); }

 private:
  static constexpr int kVersion = 1;
  static constexpr std::string_view kVersionFilename = ".db_version";

  using Repos = std::vector<std::unique_ptr<db::MappedRepo>>;

  Database(Repos repos) : repos_(std::move(repos)) {}

  Repos repos_;
};

}  // namespace pkgfile
