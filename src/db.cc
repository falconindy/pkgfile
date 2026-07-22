#include "db.hh"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace pkgfile {

std::string DatabaseError::message(int condition) const {
  switch (condition) {
    case OK:
      return "OK";
    case VERSION_FILE_NOT_FOUND:
      return "Database version file not found";
    case WRONG_VERSION:
      return "Database has incorrect version";
    default:
      return "Unknown error";
  }
}

std::unique_ptr<Database> Database::Open(std::string_view dbpath,
                                         std::error_code& ec) {
  switch (fs::status(dbpath).type()) {
    case fs::file_type::directory:
      break;
    case fs::file_type::not_found:
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return nullptr;
    default:
      ec = std::make_error_code(std::errc::not_a_directory);
      return nullptr;
  }

  std::ifstream version_file(fs::path(dbpath) / kVersionFilename,
                             std::ios::binary);
  if (!version_file.is_open()) {
    ec.assign(DatabaseError::VERSION_FILE_NOT_FOUND, DatabaseError::Get());
    return nullptr;
  }

  int version;
  version_file >> version;
  version_file.close();

  if (version != kVersion) {
    ec.assign(DatabaseError::WRONG_VERSION, DatabaseError::Get());
    return nullptr;
  }

  Repos repos;
  for (const auto& entry : fs::directory_iterator(dbpath, ec)) {
    const fs::path& pathname = entry.path();
    if (!entry.is_regular_file() || pathname.extension() != ".files") {
      continue;
    }

    db::MappedRepo::OpenError open_error;
    auto repo = db::MappedRepo::Open(pathname.string(), &open_error);
    if (repo == nullptr) {
      std::cerr << std::format(
          "warning: failed to open repo database {}, ignoring (you may need "
          "to run `pkgfile --update`)\n",
          pathname.string());
      continue;
    }

    repos.push_back(std::move(repo));
  }

  if (ec.value() != 0) {
    return nullptr;
  }

  std::sort(repos.begin(), repos.end(), [](const auto& a, const auto& b) {
    return a->reponame() < b->reponame();
  });

  return std::unique_ptr<Database>(new Database(std::move(repos)));
}

const db::MappedRepo* Database::GetRepo(std::string_view reponame) const {
  const auto iter =
      std::lower_bound(repos_.begin(), repos_.end(), reponame,
                       [](const auto& repo, std::string_view name) {
                         return repo->reponame() < name;
                       });

  if (iter == repos_.end() || (*iter)->reponame() != reponame) {
    return nullptr;
  }
  return iter->get();
}

bool Database::WriteDatabaseVersion(std::string_view dbpath) {
  std::ofstream version_file(fs::path(dbpath) / Database::kVersionFilename);

  if (version_file.bad()) {
    return false;
  }

  version_file << Database::kVersion;
  version_file.close();

  return true;
}

}  // namespace pkgfile
