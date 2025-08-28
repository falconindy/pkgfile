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
  buffer_.clear();

  for (;;) {
    auto pos = block_.find('\n');

    // Take from the block if there's still newline-delimited bytes available.
    if (pos != block_.npos) {
      if (buffer_.empty()) {
        *line = block_.substr(0, pos);
      } else {
        // We're guaranteed that buffer_ doesn't contain a new line, so grab the
        // next data chunk up to a newline from the new block.
        buffer_.append(block_.data(), pos);
        *line = buffer_;
      }
      block_.remove_prefix(pos + 1);
      return ARCHIVE_OK;
    }

    // No new line in current block.
    // Copy the block data to buffer and refill.
    buffer_.append(block_.data(), block_.size());
    block_ = {};

    if (int r = ConsumeBlock(); r != ARCHIVE_OK) {
      return r;
    }
  }
}

}  // namespace pkgfile
