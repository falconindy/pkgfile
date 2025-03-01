#include "db.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
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

bool Database::FilenameHasRepoSuffix(std::string_view path) {
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
    if (!entry.is_regular_file() || !FilenameHasRepoSuffix(pathname.native())) {
      continue;
    }

    repos.emplace_back(pathname.stem().stem().string(), pathname);
  }

  if (ec.value() != 0) {
    return nullptr;
  }

  std::sort(repos.begin(), repos.end());

  return std::unique_ptr<Database>(new Database(std::move(repos)));
}

Database::RepoChunks Database::GetAllRepoChunks() const {
  return {repos_.begin(), repos_.end()};
}

Database::RepoChunks Database::GetRepoChunks(std::string_view reponame) const {
  const auto lower = std::find_if(
      repos_.begin(), repos_.end(),
      [&reponame](const Entry& entry) { return entry.reponame == reponame; });

  auto upper = lower;
  for (; upper != repos_.end(); ++upper) {
    if (upper->reponame != reponame) {
      break;
    }
  }

  return {lower, upper};
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
