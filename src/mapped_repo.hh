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

  // Advises the kernel about the access pattern about to be used against
  // this mapping. Open() doesn't do this itself: indexed lookups
  // (FindPackageByName, FindBasename, ...) binary-search sorted tables and
  // want MADV_RANDOM so the kernel doesn't fault in neighboring pages that
  // will never be touched, but a full scan (ScanAllFiles or
  // EmitPackageFileList walking every package's file list) wants the
  // opposite -- disabling readahead there means every page is its own
  // synchronous fault instead of being pulled in ahead of time. Which one
  // applies depends on the query strategy, decided after the repo is
  // already open, so the caller advises once it knows. Advisory only --
  // cheap, and a failure here isn't fatal; the choice only matters for a
  // cold page cache.
  void AdviseRandomAccess() const;
  void AdviseSequentialAccess() const;

  std::string_view ResolveString(StringId id) const;

  // Speeds up a sequence of ResolvePathInto() calls that tend to share an
  // immediate parent directory -- the common case when walking one
  // package's file list in order, since a package's files are grouped by
  // directory (measured >80% of consecutive files sharing a parent across
  // real repos). Caller-owned and safe to reuse across an entire chunk of
  // packages, not just one: the path trie is deduplicated repo-wide, so
  // even files from different packages that happen to land in the same
  // directory (e.g. two packages both dropping a file straight into
  // /usr/bin/) share a cache hit. A cache miss just falls back to a full
  // trie walk, so passing a fresh PathCache is always safe, just
  // sometimes free.
  struct PathCache {
    PathId parent = kRootPath;
    bool valid = false;
    std::string prefix;
  };

  // Resolves a tagged PathId (as found in a package's file list or a
  // Posting) into `*out`, overwriting its contents but reusing its
  // existing capacity. Prefer this over ResolvePath() in any loop that
  // resolves many paths (a glob/regex scan, say) -- a plain local
  // `std::string` declared before the loop is enough, since nothing needs
  // it to outlive that scope; reusing it there avoids paying for a fresh
  // allocation on every file. Pass a `cache` from the same loop when
  // resolving many paths in sequence to skip re-walking a shared parent
  // directory; see PathCache.
  void ResolvePathInto(uint32_t tagged_path, std::string* out,
                       PathCache* cache = nullptr) const;

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
    const uint64_t count =
        std::min<uint64_t>(pkg.files_count, table_count - start);
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
