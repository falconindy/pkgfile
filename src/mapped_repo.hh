#pragma once

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "archive_io.hh"
#include "db_format.hh"

namespace pkgfile::db {

// A read-only, zero-copy view over one PFDB repo database: the file is
// mmap'd once and every accessor below is a pointer cast or a slice into that
// mapping -- there is no parsing or decompression step.
class MappedRepo {
 public:
  enum class OpenError {
    kFileError,
    kNotMapped,
    kTooSmall,
    kBadMagic,
    kWrongVersion,
    kWrongByteOrder,
    kTruncated,
  };

  static std::unique_ptr<MappedRepo> Open(const std::string& path,
                                          OpenError* error);

  std::string_view reponame() const {
    return ResolveString(header_->repo_name);
  }

  std::string_view ResolveString(StringId id) const;

  // Resolves a tagged PathId (as found in a package's file list or a
  // Posting) into `*out`, overwriting its contents but reusing its
  // existing capacity. Prefer this over ResolvePath() in any loop that
  // resolves many paths (a glob/regex scan, say) -- a plain local
  // `std::string` declared before the loop is enough, since nothing needs
  // it to outlive that scope; reusing it there avoids paying for a fresh
  // allocation on every file.
  void ResolvePathInto(uint32_t tagged_path, std::string* out) const;

  // Resolves a tagged PathId (as found in a package's file list or a
  // Posting) to its full path, e.g. "/usr/bin/foo", or "/usr/bin/" if the
  // occurrence's directory bit is set. Convenience wrapper around
  // ResolvePathInto() for callers that just want one owned string and
  // don't care about reuse.
  std::string ResolvePath(uint32_t tagged_path) const {
    std::string out;
    ResolvePathInto(tagged_path, &out);
    return out;
  }

  // Bounds-checked: `id` ultimately comes from the file (a package's file
  // list, a Posting, or another PathNode's `parent`), so it can't be trusted
  // to be in range. An out-of-range id, or a `parent` that doesn't point to
  // a strictly earlier node -- the order the builder appends them in, so
  // it's true for every legitimately-built db -- returns this sentinel
  // instead of indexing out of the mapping. The sentinel's own `parent` is
  // kRootPath, so a trie walk that hits it terminates immediately rather
  // than chasing a bad or cyclic reference any further.
  const PathNode& PathNodeAt(PathId id) const {
    static constexpr PathNode kInvalid{kRootPath, 0};
    if (id >= header_->path_table_count) {
      return kInvalid;
    }
    const PathNode& node = PtrAt<PathNode>(header_->path_table_offset)[id];
    if (node.parent != kRootPath && node.parent >= id) {
      return kInvalid;
    }
    return node;
  }

  // The number of distinct interned paths in this repo. Exposed mainly so
  // tests can verify that shared directory prefixes are only stored once.
  size_t path_count() const { return header_->path_table_count; }

  // The number of distinct interned strings (path components, package
  // names/versions, the repo name) and the total size of the byte pool
  // backing them. Exposed for introspection tools (see pfdb_dump.cc).
  size_t string_count() const { return header_->string_table_count; }
  uint64_t byte_pool_size() const { return header_->byte_pool_size; }

  std::span<const Package> packages() const {
    return {PtrAt<Package>(header_->package_table_offset),
            header_->package_table_count};
  }

  // Bounds-checked against the package files pool: `pkg` is trusted to be
  // one of ours (it came from packages() or FindPackageByName()), but its
  // files_start/files_count fields still come straight from the file, so a
  // corrupt db could otherwise walk this span past the pool. Clamped rather
  // than rejected outright, so a bad slice degrades to a truncated file list
  // instead of reading out of bounds.
  std::span<const uint32_t> PackageFiles(const Package& pkg) const {
    const uint64_t table_count = header_->package_files_count;
    const uint64_t start = std::min<uint64_t>(pkg.files_start, table_count);
    const uint64_t count = std::min<uint64_t>(pkg.files_count, table_count - start);
    return {PtrAt<uint32_t>(header_->package_files_offset) + start, count};
  }

  // Binary-searches the package table for an exact, case-sensitive name
  // match. Packages are sorted by name, so callers wanting a case-insensitive
  // or fuzzy match should scan packages() directly instead.
  const Package* FindPackageByName(std::string_view name) const;

  std::span<const BasenameEntry> basename_index() const {
    return {PtrAt<BasenameEntry>(header_->basename_index_offset),
            header_->basename_index_count};
  }

  // Returns every (package, path) occurrence of `entry`'s basename. If
  // there's exactly one, it's inlined in `entry` itself (see db_format.hh)
  // and gets materialized into `*single`; the returned span points at
  // `single` in that case, or into the shared postings pool otherwise, so
  // callers must keep `single` alive for as long as the returned span is
  // used.
  //
  // Bounds-checked, including the pkg/path each Posting carries -- callers
  // index packages() with a Posting's pkg field directly, so a corrupt
  // entry pointing outside the package or path table would otherwise be an
  // out-of-bounds read one step removed from here. Checking costs nothing
  // extra beyond what the caller was already about to pay to iterate this
  // same (small) slice. Returns an empty span if anything in it is invalid,
  // rather than a partially-valid one.
  std::span<const Posting> PostingsFor(const BasenameEntry& entry,
                                       Posting* single) const;

  // Binary-searches the basename index for an exact, case-sensitive match.
  // Returns nullptr if no file in this repo has this basename.
  const BasenameEntry* FindBasename(std::string_view name) const;

 private:
  MappedRepo(std::unique_ptr<ReadOnlyFile> file, const Header* header)
      : file_(std::move(file)), header_(header) {}

  // Slow-path fallback for ResolvePathInto(): a second, independent walk of
  // the trie to write components directly, for paths deeper than
  // kMaxInlineDepth can hold. `length` is the already-computed final size.
  void ResolvePathIntoSlow(uint32_t tagged_path, size_t length,
                           std::string* out) const;

  template <typename T>
  const T* PtrAt(uint64_t byte_offset) const {
    return reinterpret_cast<const T*>(base() + byte_offset);
  }

  const uint8_t* base() const {
    return static_cast<const uint8_t*>(file_->mmapped()->ptr);
  }

  std::unique_ptr<ReadOnlyFile> file_;
  const Header* header_;
};

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
