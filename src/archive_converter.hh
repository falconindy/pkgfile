#pragma once

#include <archive.h>
#include <archive_entry.h>
#include <string.h>

#include <filesystem>
#include <string>

#include "archive_io.hh"
#include "cista.h"
#include "pkgfile.hh"
#include "repo.hh"

namespace pkgfile {

// ArchiveConverter converts the files database for a given repo from the native
// ALPM format (a compressed tarball) to the pkgfile format.
class ArchiveConverter {
 public:
  ArchiveConverter(std::string reponame, std::unique_ptr<ReadArchive> in,
                   std::string base_filename_out, int repo_chunk_bytes)
      : reponame_(std::move(reponame)),
        in_(std::move(in)),
        base_filename_out_(std::move(base_filename_out)),
        repo_chunk_bytes_(repo_chunk_bytes <= 0 ? kDefaultRepoChunkMax
                                                : repo_chunk_bytes) {}

  ArchiveConverter(const ArchiveConverter&) = delete;
  ArchiveConverter& operator=(const ArchiveConverter&) = delete;

  ArchiveConverter(ArchiveConverter&&) = default;
  ArchiveConverter& operator=(ArchiveConverter&&) = default;

  bool RewriteArchive();

 private:
  static constexpr int kDefaultRepoChunkMax = 40 * (1 << 20);

  int WriteMetaEntry(const std::filesystem::path& entryname);
  bool Finalize();

  static std::string MakeArchiveChunkFilename(const std::string& base_filename,
                                              int chunk_number, bool tempfile);

  bool NextArchiveChunk();

  std::string reponame_;
  std::unique_ptr<ReadArchive> in_;
  std::string base_filename_out_;
  int repo_chunk_bytes_;

  RepoMeta data_;
  int chunk_number_ = 0;
};

}  // namespace pkgfile
