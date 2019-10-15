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
  ArchiveConverter(std::unique_ptr<ReadArchive> in,
                   std::unique_ptr<WriteArchive> out)
      : in_(std::move(in)), out_(std::move(out)) {}

  ArchiveConverter(const ArchiveConverter&) = delete;
  ArchiveConverter& operator=(const ArchiveConverter&) = delete;

  ArchiveConverter(ArchiveConverter&&) = default;
  ArchiveConverter& operator=(ArchiveConverter&&) = default;

  static std::unique_ptr<ArchiveConverter> New(const std::string& reponame,
                                               int fd_in,
                                               const std::string& filename_out,
                                               int compress);

  bool RewriteArchive();

 private:
  int WriteCpioEntry(archive_entry* ae, const std::filesystem::path& entryname);
  bool Finalize();

  std::string reponame_;
  std::string destfile_;
  std::unique_ptr<ReadArchive> in_;
  std::unique_ptr<WriteArchive> out_;
};

}  // namespace pkgfile
