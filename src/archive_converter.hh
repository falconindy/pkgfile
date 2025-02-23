#pragma once

#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#include <filesystem>
#include <string>

#include "archive_io.hh"
#include "pkgfile.hh"

namespace pkgfile {

// ArchiveConverter converts the files database for a given repo from the native
// ALPM format (a compressed tarball) to the pkgfile format (lists of files
// packed in CPIO).
class ArchiveConverter {
 public:
  ArchiveConverter(std::string reponame, std::string base_filename_out,
                   int compress, int repo_chunk_bytes,
                   std::unique_ptr<ReadArchive> in,
                   std::unique_ptr<WriteArchive> out)
      : reponame_(std::move(reponame)),
        base_filename_out_(std::move(base_filename_out)),
        compress_(compress),
        repo_chunk_bytes_(repo_chunk_bytes <= 0 ? kDefaultRepoChunkMax
                                                : repo_chunk_bytes),
        in_(std::move(in)),
        out_(std::move(out)) {}

  ArchiveConverter(const ArchiveConverter&) = delete;
  ArchiveConverter& operator=(const ArchiveConverter&) = delete;

  ArchiveConverter(ArchiveConverter&&) = default;
  ArchiveConverter& operator=(ArchiveConverter&&) = default;

  static std::unique_ptr<ArchiveConverter> New(const std::string& reponame,
                                               int fd_in,
                                               std::string base_filename_out,
                                               int compress,
                                               int repo_chunk_bytes);

  bool RewriteArchive();

 private:
  static constexpr int kDefaultRepoChunkMax = 40 * (1 << 20);

  int WriteCpioEntry(archive_entry* ae, const std::filesystem::path& entryname);
  bool Finalize();

  static std::string MakeArchiveChunkFilename(const std::string& base_filename,
                                              int chunk_number, bool tempfile);

  bool NextArchiveChunk();

  std::string reponame_;
  std::string base_filename_out_;
  int compress_;
  int repo_chunk_bytes_;

  std::unique_ptr<ReadArchive> in_;
  std::unique_ptr<WriteArchive> out_;

  int chunk_number_ = 0;
};

}  // namespace pkgfile
