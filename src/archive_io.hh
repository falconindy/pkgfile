#pragma once

#include <archive.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace pkgfile {

class ReadOnlyFile {
 public:
  ~ReadOnlyFile();

  struct MMappedRegion {
    MMappedRegion(void* ptr, off_t size) : ptr(ptr), size(size) {}
    ~MMappedRegion();

    // Moveable, but not copyable.
    MMappedRegion(const MMappedRegion&) = delete;
    MMappedRegion& operator=(const MMappedRegion&) = delete;

    MMappedRegion& operator=(MMappedRegion&& other) {
      ptr = std::exchange(other.ptr, MAP_FAILED);
      size = std::exchange(other.size, -1);
      return *this;
    }
    MMappedRegion(MMappedRegion&& other) : ptr(other.ptr), size(other.size) {
      *this = std::move(other);
    }

    void* ptr;
    off_t size;
  };

  int fd() const { return fd_; }

  const std::optional<MMappedRegion>& mmapped() const { return mapped_; }

  static std::unique_ptr<ReadOnlyFile> Open(const std::string& path,
                                            bool try_mmap);

 private:
  ReadOnlyFile(int fd, std::optional<MMappedRegion> mapped)
      : fd_(fd), mapped_(std::move(mapped)) {}

  int fd_;
  std::optional<MMappedRegion> mapped_;
};

class ReadArchive {
 public:
  ~ReadArchive();

  ReadArchive(const ReadArchive&) = delete;
  ReadArchive& operator=(const ReadArchive&) = delete;

  ReadArchive(ReadArchive&&) = default;
  ReadArchive& operator=(ReadArchive&&) = default;

  static std::unique_ptr<ReadArchive> New(int fd, const char** error);

  static std::unique_ptr<ReadArchive> New(const ReadOnlyFile& file,
                                          const char** error);

  int fd() const { return fd_; }
  archive* read_archive() { return a_; }

  void Close();

 private:
  ReadArchive(int fd) : fd_(fd) {
    archive_read_support_format_tar(a_);
    archive_read_support_format_cpio(a_);
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

}  // namespace pkgfile
