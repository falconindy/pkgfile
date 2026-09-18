#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "db_format.hh"

namespace pkgfile::db {

// Builds a PFDB repo database in memory from a repo's upstream `.files`
// archive, then serializes it to disk.
//
// Usage:
//   auto builder = DbBuilder::FromArchive(reponame, fd, &error);
//   if (builder == nullptr) { ... }
//   builder->WriteToFile(path, mtime);
class DbBuilder {
 public:
  explicit DbBuilder(std::string reponame);

  DbBuilder(const DbBuilder&) = delete;
  DbBuilder& operator=(const DbBuilder&) = delete;

  // Reads every package's file list out of the upstream ALPM `.files`
  // archive on `fd_in` (a tarball of `$pkgname-$pkgver-$pkgrel/files` member
  // files) and interns it into a new builder. Returns nullptr and sets
  // *error on failure.
  //
  // If given, `on_progress` is called after each package with the
  // cumulative number of (compressed) bytes consumed from `fd_in` so far --
  // compare against the archive's known total size for a repack progress
  // indicator.
  static std::unique_ptr<DbBuilder> FromArchive(
      std::string reponame, int fd_in, const char** error,
      const std::function<void(int64_t bytes_read)>& on_progress = nullptr);

  // Interns one package's file list. `files` pairs a path (no leading slash,
  // no trailing slash even for directories) with whether it's a directory.
  void AddPackage(std::string_view name, std::string_view version,
                  const std::vector<std::pair<std::string, bool>>& files);

  // Serializes the accumulated database and atomically replaces `path` with
  // it (writes to `path + "~"` then renames over `path`). `mtime` is the
  // mtime of the upstream archive this was built from; it's stamped onto the
  // resulting file (both atime and mtime) so a later stat() of `path` can be
  // used to decide whether a repo needs re-downloading. Returns false on
  // failure, leaving `path` untouched.
  bool WriteToFile(const std::string& path, int64_t mtime);

 private:
  StringId InternString(std::string_view s);
  PathId InternPath(std::string_view path);

  std::string reponame_;

  // A deque (rather than vector) so that a string's address, once interned,
  // never moves -- string_lookup_ below keys on views into these elements
  // instead of paying for a second copy of every interned string's bytes.
  std::deque<std::string> strings_;
  std::unordered_map<std::string_view, StringId> string_lookup_;

  std::vector<PathNode> paths_;
  std::unordered_map<uint64_t, PathId> path_lookup_;

  struct PendingPackage {
    StringId name;
    StringId version;
    std::vector<uint32_t> tagged_files;  // PathId, tagged with kDirBit
  };
  std::vector<PendingPackage> packages_;

  // Every (package, path) occurrence of every basename, threaded into a
  // singly linked list per basename instead of a separate vector<Posting>
  // per basename: real repos have millions of basenames with exactly one
  // occurrence each (see db_format.hh), and a `vector` per key means a
  // separate heap allocation for each of them. Packages are referenced by
  // their index into `packages_` until WriteToFile() remaps them to their
  // final, name-sorted PkgId.
  struct PendingPosting {
    Posting posting;
    uint32_t prev;  // index into pending_postings_, or kNoPosting
  };
  static constexpr uint32_t kNoPosting = 0xFFFFFFFFu;
  std::vector<PendingPosting> pending_postings_;
  // Basename StringId -> index into pending_postings_ of its most recently
  // added occurrence (the head of that basename's list).
  std::unordered_map<StringId, uint32_t> basename_heads_;
};

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
