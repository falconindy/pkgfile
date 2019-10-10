#include "archive_reader.hh"

namespace pkgfile {

int ArchiveReader::Next(archive_entry** entry) {
  line_.clear();
  block_ = std::string_view();

  status_ = archive_read_next_header(archive_, entry);
  return status_;
}

int ArchiveReader::ConsumeBlock() {
  // end of the archive
  if (status_ == ARCHIVE_EOF) {
    return ARCHIVE_EOF;
  }

  // there's still more that reader_line_consume can use
  if (!block_.empty()) {
    return ARCHIVE_OK;
  }

  // grab a new block of data from the archive
  const void* buf;
  size_t len;
  int64_t offset;

  status_ = archive_read_data_block(archive_, &buf, &len, &offset);
  block_ = std::string_view(static_cast<const char*>(buf), len);

  return status_;
}

int ArchiveReader::FillLine() {
  auto pos = block_.find('\n');

  if (pos == std::string_view::npos) {
    // grab the entire block, and the caller will try again
    line_.append(block_);
    block_ = std::string_view();
    return EAGAIN;
  }

  line_.append(block_.substr(0, pos));
  block_.remove_prefix(pos + 1);
  return 0;
}

int ArchiveReader::GetLine(std::string* line) {
  line->clear();

  for (;;) {
    int r;

    r = ConsumeBlock();
    if (r != ARCHIVE_OK) {
      return r;
    }

    r = FillLine();
    if (r == EAGAIN) {
      continue;
    }

    *line = std::move(line_);
    return r;
  }
}

}  // namespace pkgfile
