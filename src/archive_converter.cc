#include "archive_converter.hh"

#include <sstream>

namespace pkgfile {

namespace internal {

// static
std::unique_ptr<ReadArchive> ReadArchive::New(int fd) {
  std::unique_ptr<ReadArchive> a(new ReadArchive());

  int r = archive_read_open_fd(a->a_, fd, BUFSIZ);
  if (r != ARCHIVE_OK) {
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
                                                int compress) {
  std::unique_ptr<WriteArchive> a(new WriteArchive(path, compress));

  int r = archive_write_open_filename(a->a_, path.c_str());
  if (r != ARCHIVE_OK) {
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
std::unique_ptr<ArchiveConverter> ArchiveConverter::New(const repo_t* repo) {
  std::string tmpfile_path = repo->diskfile;
  tmpfile_path.append("~");

  auto reader = internal::ReadArchive::New(repo->tmpfile.fd);
  if (reader == nullptr) {
    fprintf(stderr, "error: failed to create archive reader for %s: %s\n",
            repo->name.c_str(), reader->archive_error());
    return nullptr;
  }

  auto writer =
      internal::WriteArchive::New(tmpfile_path, repo->config->compress);
  if (writer == nullptr) {
    fprintf(stderr, "error: failed to open file for writing: %s: %s\n",
            tmpfile_path.c_str(), writer->archive_error());
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

bool ArchiveConverter::Finalize(const std::string& dest) {
  in_->Close();

  if (!out_->Close()) {
    return false;
  }

  if (rename(out_->path().c_str(), dest.c_str()) < 0) {
    fprintf(stderr, "error: renaming tmpfile to %s failed: %s\n", dest.c_str(),
            strerror(errno));
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
      int r = WriteCpioEntry(ae, entryname);
      if (r < 0) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace pkgfile
