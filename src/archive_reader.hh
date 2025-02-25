#pragma once

#include <archive.h>

#include <queue>
#include <string>
#include <string_view>

namespace pkgfile {

class ArchiveReader {
 public:
  ArchiveReader(archive* archive) : archive_(archive) {}

  // Skip to the next header in the archive. If this returns ARCHIVE_OK, you may
  // call GetLine to read the contents of the entry.
  int Next(archive_entry** entry);

  // Read the body of the archive entry line by line, returning ARCHIVE_OK on
  // success.
  int GetLine(std::string_view* line);

 private:
  int ConsumeBlock();

  archive* archive_;

  long status_ = ARCHIVE_OK;
  std::string_view block_;
  std::string buffer_;
};

}  // namespace pkgfile
