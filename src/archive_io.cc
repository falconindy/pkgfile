#include "archive_io.hh"

#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace pkgfile {

// static
std::unique_ptr<ReadOnlyFile> ReadOnlyFile::Open(const std::string& path,
                                                 const bool try_mmap) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return nullptr;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    close(fd);
    return nullptr;
  }

  std::optional<ReadOnlyFile::MMappedRegion> mapped;
  if (try_mmap) {
    void* ptr =
        mmap(0, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (ptr != MAP_FAILED) {
      mapped = std::make_optional<ReadOnlyFile::MMappedRegion>(ptr, st.st_size);
    }
  }

  return std::unique_ptr<ReadOnlyFile>(new ReadOnlyFile(fd, std::move(mapped)));
}

ReadOnlyFile::MMappedRegion::~MMappedRegion() {
  if (ptr != MAP_FAILED) {
    munmap(ptr, size);
  }
}

ReadOnlyFile::~ReadOnlyFile() { close(fd_); }

// static
std::unique_ptr<ReadArchive> ReadArchive::New(int fd, const char** error) {
  std::unique_ptr<ReadArchive> a(new ReadArchive(fd));

  if (archive_read_open_fd(a->a_, fd, BUFSIZ) != ARCHIVE_OK) {
    *error = strerror(archive_errno(a->a_));
    return nullptr;
  }

  a->opened_ = true;
  return a;
}

ReadArchive::~ReadArchive() {
  Close();
  archive_read_free(a_);
}

void ReadArchive::Close() {
  if (opened_) {
    opened_ = false;
    archive_read_close(a_);
  }
}

// static
std::unique_ptr<ReadArchive> ReadArchive::New(const ReadOnlyFile& file,
                                              const char** error) {
  const auto& mapped = file.mmapped();
  if (!mapped) {
    return New(file.fd(), error);
  }

  std::unique_ptr<ReadArchive> a(new ReadArchive(file.fd()));

  if (archive_read_open_memory(a->a_, mapped->ptr, mapped->size) !=
      ARCHIVE_OK) {
    *error = strerror(archive_errno(a->a_));
    return nullptr;
  }

  a->opened_ = true;
  return a;
}

// static
std::unique_ptr<WriteArchive> WriteArchive::New(const std::string& path,
                                                int compress,
                                                const char** error) {
  std::unique_ptr<WriteArchive> a(new WriteArchive(path, compress));

  if (archive_write_open_filename(a->a_, path.c_str()) != ARCHIVE_OK) {
    *error = strerror(archive_errno(a->a_));
    return nullptr;
  }

  a->opened_ = true;
  return a;
}

bool WriteArchive::Close() {
  if (opened_) {
    opened_ = false;
    return archive_write_close(a_) == ARCHIVE_OK;
  }

  return true;
}

WriteArchive::~WriteArchive() {
  Close();
  archive_write_free(a_);
}

}  // namespace pkgfile
