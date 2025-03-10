#include "archive_converter.hh"

#include <sys/stat.h>
#include <sys/time.h>

#include <filesystem>
#include <format>
#include <iostream>

namespace fs = std::filesystem;

namespace pkgfile {

// static
std::unique_ptr<ArchiveConverter> ArchiveConverter::New(
    const std::string& reponame, int fd_in, std::string base_filename_out,
    int compress, int repo_chunk_bytes) {
  const char* error;

  auto reader = ReadArchive::New(fd_in, &error);
  if (reader == nullptr) {
    std::cerr << std::format(
        "error: failed to create archive reader for {}: {}\n", reponame, error);
    return nullptr;
  }

  auto writer = WriteArchive::New(
      MakeArchiveChunkFilename(base_filename_out, 0, true), compress, &error);
  if (writer == nullptr) {
    std::cerr << std::format("error: failed to open file for writing: {}: {}\n",
                             base_filename_out, error);
    return nullptr;
  }

  return std::make_unique<ArchiveConverter>(
      reponame, std::move(base_filename_out), compress, repo_chunk_bytes,
      std::move(reader), std::move(writer));
}

std::string ArchiveConverter::MakeArchiveChunkFilename(
    const std::string& base_filename, int chunk_number, bool tempfile) {
  return std::format("{}.{:03d}{}", base_filename, chunk_number,
                     tempfile ? "~" : "");
}

bool ArchiveConverter::NextArchiveChunk() {
  if (!out_->Close()) {
    return false;
  }

  const char* error;

  auto writer = WriteArchive::New(
      MakeArchiveChunkFilename(base_filename_out_, ++chunk_number_, true),
      compress_, &error);
  if (writer == nullptr) {
    std::cerr << std::format("error: failed to open file for writing: {}: {}\n",
                             base_filename_out_, error);
    return false;
  }

  out_ = std::move(writer);

  return true;
}

int ArchiveConverter::WriteCpioEntry(archive_entry* ae,
                                     const fs::path& entryname) {
  pkgfile::ArchiveReader reader(in_->read_archive());
  std::string_view line;

  // discard the first line
  reader.GetLine(&line);

  std::string entry;
  while (reader.GetLine(&line) == ARCHIVE_OK) {
    // do the copy, with a slash prepended
    std::format_to(std::back_inserter(entry), "/{}\n", line);
  }

  // adjust the entry size for removing the first line and adding slashes
  archive_entry_set_size(ae, entry.size());

  // inodes in cpio archives are dumb.
  archive_entry_set_ino64(ae, 0);

  // store the metadata as simply $pkgname-$pkgver-$pkgrel
  archive_entry_update_pathname_utf8(ae, entryname.parent_path().c_str());

  if (archive_write_header(out_->write_archive(), ae) != ARCHIVE_OK) {
    std::cerr << std::format("error: failed to write entry header: {}/{}: {}\n",
                             reponame_, archive_entry_pathname(ae),
                             strerror(errno));
    return -errno;
  }

  if (archive_write_data(out_->write_archive(), entry.c_str(), entry.size()) !=
      static_cast<ssize_t>(entry.size())) {
    std::cerr << std::format("error: failed to write entry: {}/{}: {}\n",
                             reponame_, archive_entry_pathname(ae),
                             strerror(errno));
    return -errno;
  }

  return entry.size();
}

bool ArchiveConverter::Finalize() {
  in_->Close();

  if (!out_->Close()) {
    return false;
  }

  struct stat st;
  fstat(in_->fd(), &st);

  const struct timeval times[] = {
      {st.st_atim.tv_sec, 0},
      {st.st_mtim.tv_sec, 0},
  };

  for (int i = 0; i <= chunk_number_; ++i) {
    std::string path = MakeArchiveChunkFilename(base_filename_out_, i, true);

    if (utimes(path.c_str(), times) < 0) {
      std::cerr << std::format("warning: failed to set filetimes on {}: {}\n",
                               out_->path(), strerror(errno));
    }

    const fs::path dest = path.substr(0, path.size() - 1);

    std::error_code ec;
    if (fs::rename(path, dest, ec); ec.value() != 0) {
      std::cerr << std::format("error: renaming tmpfile to {} failed: {}\n",
                               dest.string(), ec.message());
    }
  }

  for (int i = chunk_number_ + 1;; ++i) {
    std::string path = MakeArchiveChunkFilename(base_filename_out_, i, false);

    std::error_code ec;
    if (!fs::remove(path, ec)) {
      break;
    }
  }

  return true;
}

bool ArchiveConverter::RewriteArchive() {
  archive_entry* ae;
  int chunk_size = 0;

  while (archive_read_next_header(in_->read_archive(), &ae) == ARCHIVE_OK) {
    if (chunk_size > repo_chunk_bytes_) {
      if (!NextArchiveChunk()) {
        return false;
      }

      chunk_size = 0;
    }

    fs::path entryname = archive_entry_pathname(ae);

    // ignore everything but the /files metadata
    if (entryname.filename() == "files") {
      const int bytes_written = WriteCpioEntry(ae, entryname);
      if (bytes_written < 0) {
        return false;
      }

      chunk_size += bytes_written;
    }
  }

  return Finalize();
}

}  // namespace pkgfile
