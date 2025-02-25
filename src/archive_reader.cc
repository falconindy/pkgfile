#include "archive_reader.hh"

namespace pkgfile {

int ArchiveReader::Next(archive_entry** entry) {
  buffer_.clear();
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

int ArchiveReader::GetLine(std::string_view* line) {
  // Take from the block if there's still newline-delimited bytes available.
  if (auto pos = block_.find('\n'); pos != block_.npos) {
    *line = {block_.data(), pos};
    block_.remove_prefix(pos + 1);
    return ARCHIVE_OK;
  }

  // Otherwise, we need to take from the internal buffer, which also indicates
  // that we should refill our block.
  buffer_ = block_;
  block_ = std::string_view();

  int r = ConsumeBlock();
  if (r != ARCHIVE_OK) {
    return r;
  }

  // We're guaranteed that buffer_ doesn't contain a new line, so grab the next
  // data chunk up to a newline from the new block.
  auto pos = block_.find('\n');
  buffer_.append(block_.data(), pos);
  block_.remove_prefix(pos + 1);

  *line = buffer_;
  return ARCHIVE_OK;
}

}  // namespace pkgfile
