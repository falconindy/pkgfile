#pragma once

// On-disk layout for the pkgfile repo database ("PFDB").
//
// A PFDB file is a single flat binary blob, mmap'd read-only and read
// directly as POD structs -- there is no decompression or deserialization
// step. All cross references are indices into one of the tables below rather
// than pointers, so the file is position-independent and can be mapped
// anywhere in the address space.
//
// Compaction comes from string interning: directory components that are
// shared by many files (e.g. "usr", "usr/lib") are stored exactly once in the
// path table, which is a "parent chain" trie -- a full path is a single
// PathId, and walking the `parent` field from that id back to the root yields
// the path's components in reverse order.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pkgfile::db {

using StringId = uint32_t;
using PathId = uint32_t;
using PkgId = uint32_t;

// Sentinel marking the root of the path trie: a PathNode whose `parent` is
// this value has no parent -- its `name` is a top-level path component.
inline constexpr PathId kRootPath = 0xFFFFFFFFu;

// Package-files pool entries and Postings tag their PathId with this bit to
// record whether that particular occurrence of the path was a directory.
// This is a property of the (package, path) occurrence rather than of the
// path itself: two different packages could occur to record the same path as
// a file in one and a directory in the other.
inline constexpr uint32_t kDirBit = 0x8000'0000u;
inline constexpr uint32_t kPathIdMask = 0x7FFF'FFFFu;

inline constexpr PathId PathIdOf(uint32_t tagged) {
  return tagged & kPathIdMask;
}
inline constexpr bool IsDirOf(uint32_t tagged) {
  return (tagged & kDirBit) != 0;
}
inline constexpr uint32_t TagPath(PathId id, bool is_dir) {
  return id | (is_dir ? kDirBit : 0u);
}

#pragma pack(push, 1)

// A reference to a byte range within the byte pool.
struct StringRef {
  uint32_t offset;
  uint32_t length;
};
static_assert(sizeof(StringRef) == 8);

// One node in the path trie. `name` is a StringId for this component alone
// (e.g. "bin", not "usr/bin"); `parent` is the PathId of the enclosing
// directory, or kRootPath if this is a top-level component.
struct PathNode {
  uint32_t parent;
  uint32_t name;
};
static_assert(sizeof(PathNode) == 8);

// One package. `files` are looked up as
// package_files_pool[files_start .. files_start + files_count).
struct Package {
  uint32_t name;
  uint32_t version;
  uint32_t files_start;
  uint32_t files_count;
};
static_assert(sizeof(Package) == 16);

// One occurrence of a basename: which package it's in, and the full path
// (tagged with the directory bit) it occurred at.
struct Posting {
  uint32_t pkg;
  uint32_t path;  // PathId, tagged with kDirBit
};
static_assert(sizeof(Posting) == 8);

// One distinct basename appearing in the repo, naming every (package, path)
// occurrence of it. Most basenames in a real repo occur exactly once, so
// storing a {start, count} slice into a separate postings pool for every one
// of them would spend a whole extra Posting (and a pointer chase to reach
// it) on data that already fits in the two spare uint32_ts an entry has
// lying around. So: `postings_start`'s high bit distinguishes two encodings.
//   - clear (the common range case): postings_start/postings_count are a
//     [start, start + count) slice into the postings pool, as usual.
//   - set (the single-occurrence case): this basename has exactly one
//     occurrence, inlined directly into the entry -- postings_start (masked
//     with kPostingsStartMask) is its PkgId, and postings_count holds its
//     tagged PathId (same encoding as Posting::path). No entry for it
//     exists in the postings pool at all.
// Use HasInlinePosting()/InlinePkgOf()/InlineTaggedPathOf()/PostingCountOf()
// below rather than reading these fields directly.
struct BasenameEntry {
  uint32_t name;
  uint32_t postings_start;
  uint32_t postings_count;
};
static_assert(sizeof(BasenameEntry) == 12);

inline constexpr uint32_t kInlinePostingBit = 0x8000'0000u;
inline constexpr uint32_t kPostingsStartMask = 0x7FFF'FFFFu;

inline constexpr bool HasInlinePosting(const BasenameEntry& entry) {
  return (entry.postings_start & kInlinePostingBit) != 0;
}
inline constexpr PkgId InlinePkgOf(const BasenameEntry& entry) {
  return entry.postings_start & kPostingsStartMask;
}
inline constexpr uint32_t InlineTaggedPathOf(const BasenameEntry& entry) {
  return entry.postings_count;
}
// The true number of (package, path) occurrences of this basename -- 1 for
// an inlined entry, regardless of what raw postings_count holds.
inline constexpr uint32_t PostingCountOf(const BasenameEntry& entry) {
  return HasInlinePosting(entry) ? 1 : entry.postings_count;
}

struct Header {
  char magic[4];  // "PFDB"
  uint32_t version;
  uint32_t byte_order_guard;  // written/checked as 0x01020304
  uint32_t reserved;

  uint32_t repo_name;  // StringId
  uint32_t package_count;

  uint64_t byte_pool_offset, byte_pool_size;
  uint64_t string_table_offset, string_table_count;
  uint64_t path_table_offset, path_table_count;
  uint64_t package_table_offset, package_table_count;
  uint64_t package_files_offset, package_files_count;
  uint64_t basename_index_offset, basename_index_count;
  uint64_t postings_offset, postings_count;
};
static_assert(sizeof(Header) % 8 == 0);

#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<StringRef>);
static_assert(std::is_trivially_copyable_v<PathNode>);
static_assert(std::is_trivially_copyable_v<Package>);
static_assert(std::is_trivially_copyable_v<BasenameEntry>);
static_assert(std::is_trivially_copyable_v<Posting>);
static_assert(std::is_trivially_copyable_v<Header>);

inline constexpr char kMagic[4] = {'P', 'F', 'D', 'B'};
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kByteOrderGuard = 0x01020304u;

// Rounds `n` up to the next multiple of `align` (align must be a power of 2).
inline constexpr size_t AlignUp(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
