#include "archive_converter.hh"

#include <sys/stat.h>
#include <sys/time.h>

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace pkgfile {
namespace internal {

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

}  // namespace internal

// static
std::unique_ptr<ArchiveConverter> ArchiveConverter::New(
    const std::string& reponame, int fd_in, const std::string& filename_out,
    int compress) {
  std::string tmpfile_path = filename_out;
  tmpfile_path.append("~");
  const char* error;

  auto reader = internal::ReadArchive::New(fd_in, &error);
  if (reader == nullptr) {
    fprintf(stderr, "error: failed to create archive reader for %s: %s\n",
            reponame.c_str(), error);
    return nullptr;
  }

  auto writer = internal::WriteArchive::New(tmpfile_path, compress, &error);
  if (writer == nullptr) {
    fprintf(stderr, "error: failed to open file for writing: %s: %s\n",
            tmpfile_path.c_str(), error);
    return nullptr;
  }

  return std::make_unique<ArchiveConverter>(std::move(reader),
                                            std::move(writer));
}

int ArchiveConverter::WriteCpioEntry(archive_entry* ae,
                                     const fs::path& entryname) {
  pkgfile::ArchiveReader reader(in_->read_archive());
  std::string line;

  // discard the first line
  reader.GetLine(&line);

  std::stringstream entry_data;
  while (reader.GetLine(&line) == ARCHIVE_OK) {
    // do the copy, with a slash prepended
    entry_data << "/" << line << '\n';
  }

  const auto entry = entry_data.str();

  // adjust the entry size for removing the first line and adding slashes
  archive_entry_set_size(ae, entry.size());

  // inodes in cpio archives are dumb.
  archive_entry_set_ino64(ae, 0);

  // store the metadata as simply $pkgname-$pkgver-$pkgrel
  archive_entry_update_pathname_utf8(ae, entryname.parent_path().c_str());

  if (archive_write_header(out_->write_archive(), ae) != ARCHIVE_OK) {
    fprintf(stderr, "error: failed to write entry header: %s/%s: %s\n",
            reponame_.c_str(), archive_entry_pathname(ae), strerror(errno));
    return -errno;
  }

  if (archive_write_data(out_->write_archive(), entry.c_str(), entry.size()) !=
      static_cast<ssize_t>(entry.size())) {
    fprintf(stderr, "error: failed to write entry: %s/%s: %s\n",
            reponame_.c_str(), archive_entry_pathname(ae), strerror(errno));
    return -errno;
  }

  return 0;
}

bool ArchiveConverter::Finalize() {
  in_->Close();

  if (!out_->Close()) {
    return false;
  }

  struct stat st;
  fstat(in_->fd(), &st);

  struct timeval times[] = {
      {st.st_atim.tv_sec, 0},
      {st.st_mtim.tv_sec, 0},
  };

  if (utimes(out_->path().c_str(), times) < 0) {
    fprintf(stderr, "warning: failed to set filetimes on %s: %s\n",
            out_->path().c_str(), strerror(errno));
  }

  auto dest = out_->path().substr(0, out_->path().size() - 1);

  std::error_code ec;
  if (fs::rename(out_->path(), dest, ec); ec.value() != 0) {
    fprintf(stderr, "error: renaming tmpfile to %s failed: %s\n", dest.c_str(),
            ec.message().c_str());
    return false;
  }

  return true;
}

bool ArchiveConverter::RewriteArchive() {
  archive_entry* ae;
  while (archive_read_next_header(in_->read_archive(), &ae) == ARCHIVE_OK) {
    fs::path entryname = archive_entry_pathname(ae);
    // ignore everything but the /files metadata
    if (entryname.filename() == "files") {
      if (WriteCpioEntry(ae, entryname) < 0) {
        return false;
      }
    }
  }

  return Finalize();
}

}  // namespace pkgfile
