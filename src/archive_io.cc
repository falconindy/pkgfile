#include "archive_io.hh"

#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <fstream>

namespace pkgfile {

std::unique_ptr<ReadArchive> ReadArchive::New(const std::string& filename,
                                              const char** error) {
  std::unique_ptr<ReadArchive> a(new ReadArchive(filename));

  if (archive_read_open_filename(a->a_, filename.c_str(), BUFSIZ) !=
      ARCHIVE_OK) {
    *error = strerror(archive_errno(a->a_));
    return nullptr;
  }

  a->opened_ = true;
  return a;
}

std::unique_ptr<ReadArchive> ReadArchive::New(const int fd,
                                              const char** error) {
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

int ReadArchive::Stat(struct stat* st) const {
  if (fd_ >= 0) {
    return fstat(fd_, st);
  } else {
    return stat(filename_.c_str(), st);
  }
}

}  // namespace pkgfile
