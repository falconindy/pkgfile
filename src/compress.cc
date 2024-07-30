#include "compress.hh"

#include <archive.h>

#include <unordered_map>

namespace pkgfile {

std::optional<int> ValidateCompression(std::string_view compress) {
  static const auto* mapping = new std::unordered_map<std::string_view, int>{
      // clang-format off
      { "bzip2", ARCHIVE_FILTER_BZIP2 },
      { "gzip",  ARCHIVE_FILTER_GZIP  },
      { "lz4",   ARCHIVE_FILTER_LZ4   },
      { "lzip",  ARCHIVE_FILTER_LZIP  },
      { "lzma",  ARCHIVE_FILTER_LZMA  },
      { "lzop",  ARCHIVE_FILTER_LZOP  },
      { "none",  ARCHIVE_FILTER_NONE  },
      { "xz",    ARCHIVE_FILTER_XZ    },
      { "zstd",  ARCHIVE_FILTER_ZSTD  },
      // clang-format on
  };

  if (const auto iter = mapping->find(compress); iter != mapping->end()) {
    return iter->second;
  }

  return std::nullopt;
}

}  // namespace pkgfile
