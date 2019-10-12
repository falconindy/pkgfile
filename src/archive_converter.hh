#pragma once

#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#include <filesystem>
#include <string>

#include "pkgfile.hh"

namespace pkgfile {

namespace internal {

class ReadArchive {
 public:
  ~ReadArchive();

  ReadArchive(const ReadArchive&) = delete;
  ReadArchive& operator=(const ReadArchive&) = delete;

  ReadArchive(ReadArchive&&) = default;
  ReadArchive& operator=(ReadArchive&&) = default;

  static std::unique_ptr<ReadArchive> New(int fd, const char** error);

  int fd() const { return fd_; }
  archive* read_archive() { return a_; }

  void Close();

 private:
  ReadArchive(int fd) : fd_(fd) {
    archive_read_support_format_tar(a_);
    archive_read_support_filter_all(a_);
  }

  int fd_;
  archive* a_ = archive_read_new();
  bool opened_ = false;
};

class WriteArchive {
 public:
  ~WriteArchive();

  WriteArchive(const WriteArchive&) = delete;
  WriteArchive& operator=(const WriteArchive&) = delete;

  WriteArchive(WriteArchive&&) = default;
  WriteArchive& operator=(WriteArchive&&) = default;

  static std::unique_ptr<WriteArchive> New(const std::string& path,
                                           int compress, const char** error);

  const std::string& path() const { return path_; }

  archive* write_archive() { return a_; }

  bool Close();

 private:
  WriteArchive(const std::string& path, int compress) : path_(path) {
    archive_write_set_format_cpio_newc(a_);
    archive_write_add_filter(a_, compress);
  }

  archive* a_ = archive_write_new();
  std::string tmppath_;
  std::string path_;
  bool opened_ = false;
};

}  // namespace internal

// ArchiveConverter converts the files database for a given repo from the native
// ALPM format (a compressed tarball) to the pkgfile format (lists of files
// packed in CPIO).
class ArchiveConverter {
 public:
  ArchiveConverter(std::unique_ptr<internal::ReadArchive> in,
                   std::unique_ptr<internal::WriteArchive> out)
      : in_(std::move(in)), out_(std::move(out)) {}

  ArchiveConverter(const ArchiveConverter&) = delete;
  ArchiveConverter& operator=(const ArchiveConverter&) = delete;

  ArchiveConverter(ArchiveConverter&&) = default;
  ArchiveConverter& operator=(ArchiveConverter&&) = default;

  static std::unique_ptr<ArchiveConverter> New(const std::string& reponame,
                                               int fd_in,
                                               const std::string& filename_out,
                                               int compress);

  bool RewriteArchive();

 private:
  int WriteCpioEntry(archive_entry* ae, const std::filesystem::path& entryname);
  bool Finalize();

  std::string reponame_;
  std::string destfile_;
  std::unique_ptr<internal::ReadArchive> in_;
  std::unique_ptr<internal::WriteArchive> out_;
};

}  // namespace pkgfile
