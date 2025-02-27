#pragma once

#include <archive.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "cista.h"
#include "repo.hh"

namespace pkgfile {

struct PackageMeta {
  using FileList = cista::offset::vector<cista::offset::string>;

  cista::offset::string version;
  FileList files;
};

using RepoMeta = cista::offset::hash_map<cista::offset::string, PackageMeta>;

class SerializedFile {
 public:
  static const SerializedFile Open(const std::string& path) {
    return SerializedFile(
        cista::mmap{path.c_str(), cista::mmap::protection::READ});
  }

  const RepoMeta& repo_meta() const { return *serialized_; }

 private:
  SerializedFile(cista::mmap mapped)
      : storage_(std::move(mapped)),
        serialized_(cista::deserialize<RepoMeta, cista::mode::CAST>(storage_)) {
  }

  cista::mmap storage_;
  const RepoMeta* serialized_;
};

class ReadArchive {
 public:
  ~ReadArchive();

  ReadArchive(const ReadArchive&) = delete;
  ReadArchive& operator=(const ReadArchive&) = delete;

  ReadArchive(ReadArchive&&) = default;
  ReadArchive& operator=(ReadArchive&&) = default;

  static std::unique_ptr<ReadArchive> New(const std::string& filename,
                                          const char** error);

  static std::unique_ptr<ReadArchive> New(int fd, const char** error);

  int Stat(struct stat* st) const;

  archive* read_archive() { return a_; }

  void Close();

 private:
  ReadArchive(const std::string& filename) : filename_(filename) { Init(); }

  ReadArchive(int fd) : fd_(fd) { Init(); }

  void Init() {
    archive_read_support_format_tar(a_);
    archive_read_support_filter_all(a_);
  }

  int fd_ = -1;
  std::string filename_;

  archive* a_ = archive_read_new();
  bool opened_ = false;
};

}  // namespace pkgfile
