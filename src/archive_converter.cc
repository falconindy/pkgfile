#include "archive_converter.hh"

#include <sys/stat.h>
#include <sys/time.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace pkgfile {

namespace {

std::pair<std::string_view, std::string_view> ParsePkgname(
    std::string_view entryname) {
  const auto pkgrel = entryname.rfind('-');
  if (pkgrel == entryname.npos) {
    return {};
  }

  const auto pkgver = entryname.substr(0, pkgrel).rfind('-');
  if (pkgver == entryname.npos) {
    return {};
  }

  return {entryname.substr(0, pkgver), entryname.substr(pkgver + 1)};
}

}  // namespace

std::string ArchiveConverter::MakeArchiveChunkFilename(
    const std::string& base_filename, int chunk_number, bool tempfile) {
  return std::format("{}.{:03d}{}", base_filename, chunk_number,
                     tempfile ? "~" : "");
}

bool ArchiveConverter::NextArchiveChunk() {
  std::string chunk_name =
      MakeArchiveChunkFilename(base_filename_out_, chunk_number_++, true);
  cista::buf mmap{cista::mmap{chunk_name.c_str()}};
  cista::serialize<cista::mode::NONE>(mmap, data_);

  data_.clear();

  return true;
}

int ArchiveConverter::WriteMetaEntry(const fs::path& entryname) {
  ArchiveReader reader(in_->read_archive());
  std::string_view line;

  // discard the first line
  reader.GetLine(&line);

  auto [name, version] = ParsePkgname(entryname.c_str());
  if (name.empty()) {
    return 0;
  }

  auto& pkg = data_[name];
  pkg.version = version;

  int bytesize = 0;
  while (reader.GetLine(&line) == ARCHIVE_OK) {
    // do the copy, with a slash prepended
    bytesize += pkg.files.emplace_back(std::format("/{}", line)).size();
  }

  return bytesize;
}

bool ArchiveConverter::Finalize() {
  NextArchiveChunk();

  struct stat st;
  in_->Stat(&st);
  in_->Close();

  const struct timeval times[] = {
      {st.st_atim.tv_sec, 0},
      {st.st_mtim.tv_sec, 0},
  };

  for (int i = 0; i < chunk_number_; ++i) {
    std::string path = MakeArchiveChunkFilename(base_filename_out_, i, true);
    std::string dest = MakeArchiveChunkFilename(base_filename_out_, i, false);

    if (utimes(path.c_str(), times) < 0) {
      std::cerr << std::format("warning: failed to set filetimes on {}: {}\n",
                               path, strerror(errno));
    }

    std::error_code ec;
    if (fs::rename(path, dest, ec); ec.value() != 0) {
      std::cerr << std::format("error: renaming tmpfile to {} failed: {}\n",
                               dest, ec.message());
    }
  }

  for (int i = chunk_number_;; ++i) {
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

    const fs::path entryname = archive_entry_pathname(ae);

    // ignore everything but the /files metadata
    if (entryname.filename() != "files") {
      continue;
    }

    const int bytes_written = WriteMetaEntry(entryname.parent_path());
    if (bytes_written < 0) {
      return false;
    }

    chunk_size += bytes_written;
  }

  return Finalize();
}

}  // namespace pkgfile
