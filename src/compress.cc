#include "compress.hh"

#include <archive.h>

#include <unordered_map>

namespace pkgfile {

std::optional<int> ValidateCompression(std::string_view compress) {
  static const auto* mapping = new std::unordered_map<std::string_view, int>{
      // clang-format off
      { "none",  ARCHIVE_FILTER_NONE  },
      { "gzip",  ARCHIVE_FILTER_GZIP  },
      { "bzip2", ARCHIVE_FILTER_BZIP2 },
      { "lzma",  ARCHIVE_FILTER_LZMA  },
      { "lzop",  ARCHIVE_FILTER_LZOP  },
      { "lz4",   ARCHIVE_FILTER_LZ4   },
      { "xz",    ARCHIVE_FILTER_XZ    },
      // clang-format on
  };

  if (auto iter = mapping->find(compress); iter != mapping->end()) {
    return iter->second;
  }

  return std::nullopt;
}

}  // namespace pkgfile
