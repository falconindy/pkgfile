#include "archive_io.hh"

#include <string.h>

namespace pkgfile {

// static
std::unique_ptr<ReadArchive> ReadArchive::New(int fd, const char** error) {
  std::unique_ptr<ReadArchive> a(new ReadArchive(fd));

  int r = archive_read_open_fd(a->a_, fd, BUFSIZ);
  if (r != ARCHIVE_OK) {
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
    archive_read_close(a_);
    opened_ = false;
  }
}

// static
std::unique_ptr<WriteArchive> WriteArchive::New(const std::string& path,
                                                int compress,
                                                const char** error) {
  std::unique_ptr<WriteArchive> a(new WriteArchive(path, compress));

  int r = archive_write_open_filename(a->a_, path.c_str());
  if (r != ARCHIVE_OK) {
    *error = strerror(archive_errno(a->a_));
    return nullptr;
  }

  a->opened_ = true;
  return a;
}

bool WriteArchive::Close() {
  if (opened_) {
    int r = archive_write_close(a_);
    opened_ = false;

    return r == ARCHIVE_OK;
  }

  return true;
}

WriteArchive::~WriteArchive() {
  Close();
  archive_write_free(a_);
}

}  // namespace pkgfile
