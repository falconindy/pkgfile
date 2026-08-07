#pragma once

// On-disk layout for the pkgfile repo database ("PFDB").
//
// A PFDB file is a single flat binary blob, mmap'd read-only. Most sections
// are read directly as POD structs with no decoding step. Two exceptions,
// both chosen because they're either always randomly accessed anyway (so
// narrowing the record instead of varint-encoding it keeps O(1) indexing)
// or read only as small, already-located slices (so a lightweight decode
// costs nothing beyond what the caller was already paying to iterate it):
//   - the string table stores one u32 byte-pool offset per string rather
//     than an {offset,length} pair -- length is implicit from the next
//     entry's offset, since the byte pool is written by pure concatenation.
//   - the path trie's nodes are packed into 6 bytes each (two 24-bit
//     fields) rather than the natural 8, since a PathId/StringId
//     comfortably fits 24 bits for any repo pkgfile realistically indexes.
//   - the postings pool -- reached only after a binary search on the
//     (still fixed-width) basename index, then read as one small bounded
//     slice -- is delta+varint encoded rather than a flat Posting array.
// All cross references are indices (or, for the postings pool, byte
// offsets) into one of the tables below rather than pointers, so the file
// is position-independent and can be mapped anywhere in the address space.
//
// Compaction also comes from string interning: directory components that
// are shared by many files (e.g. "usr", "usr/lib") are stored exactly once
// in the path table, which is a "parent chain" trie -- a full path is a
// single PathId, and walking the `parent` field from that id back to the
// root yields the path's components in reverse order.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

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

// One node in the path trie. `name` is a StringId for this component alone
// (e.g. "bin", not "usr/bin"); `parent` is the PathId of the enclosing
// directory, or kRootPath if this is a top-level component.
//
// This is the in-memory/API shape only -- DbBuilder builds the trie as a
// plain vector of these, and MappedRepo::PathNodeAt() unpacks into one to
// hand back to callers. On disk each node is packed into 6 bytes instead
// (see kPackedPathNodeSize / PackPathNode24 / UnpackPathNode24 below), so
// there's nothing in the mapping shaped like this struct to return a
// reference into.
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
// (tagged with the directory bit) it occurred at. This is the decoded,
// in-memory shape; on disk, pooled postings are delta+varint encoded (see
// EncodePostingsDelta/DecodePostingsDelta below) rather than stored as a
// flat array of these.
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
//   - clear (the common range case): `postings_start` is a byte offset into
//     the postings pool's delta+varint blob, where `postings_count` entries
//     are encoded starting there (see DecodePostingsDelta).
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

#pragma pack(pop)

// -- Path table: 6-byte packed nodes --
//
// Each PathNode is packed on disk into 6 bytes (three little-endian bytes
// each for `parent` and `name`) instead of the natural 8, since a PathId or
// StringId comfortably fits in 24 bits for any repo pkgfile will
// realistically see (Arch's `extra` has ~6.7M distinct paths and ~2.8M
// distinct strings as of writing -- roughly 2.5x headroom under the 24-bit
// ceiling below). The packed layout is fixed-width, so PathNodeAt() stays
// O(1): index by id, unpack.
inline constexpr size_t kPackedPathNodeSize = 6;

// The packed `parent` field's "no parent" marker (the 24-bit analog of
// kRootPath), and the shared capacity ceiling DbBuilder::WriteToFile()
// enforces for both the path table (whose 24-bit `parent` field reserves
// this value as a sentinel, so the table itself may hold at most this many
// nodes) and the string table (whose 24-bit `name` field has no reserved
// value and could technically hold one more, but sharing one ceiling is
// simpler to reason about than tracking two).
inline constexpr uint32_t kMaxPackedPathCount = 0x00FF'FFFFu;  // 16,777,215

// Packs/unpacks via two fixed-size memcpy calls (a uint32_t covering bytes
// [0,4), a uint16_t covering bytes [4,6)) rather than six individual byte
// loads/shifts. This is the standard type-punning-free way to do an
// unaligned load/store in C++ -- no UB, and compilers reliably fold each
// memcpy into a single unaligned move instruction -- which matters here
// because PathNodeAt() (below) calls this unconditionally on every
// ResolvePathInto(), including cache hits: a full-repo scan calls it once
// per file (millions of times for a large repo), so this is squarely on
// the hot path a glob/regex query drives. Values are packed in native byte
// order, same as every other multi-byte field in this format (see the
// byte_order_guard check in MappedRepo::Open) -- this isn't attempting a
// portable on-disk byte order, just an unaligned read/write of whatever
// order native uint32_t/uint16_t already use.
inline void PackPathNode24(uint8_t out[kPackedPathNodeSize], uint32_t parent,
                           uint32_t name) {
  const uint32_t packed_parent =
      (parent == kRootPath) ? kMaxPackedPathCount : parent;
  const uint32_t lo4 = packed_parent | ((name & 0xFFu) << 24);
  memcpy(out, &lo4, sizeof(lo4));
  const uint16_t hi2 = static_cast<uint16_t>(name >> 8);
  memcpy(out + sizeof(lo4), &hi2, sizeof(hi2));
}

inline PathNode UnpackPathNode24(const uint8_t in[kPackedPathNodeSize]) {
  uint32_t lo4;
  memcpy(&lo4, in, sizeof(lo4));
  uint16_t hi2;
  memcpy(&hi2, in + sizeof(lo4), sizeof(hi2));

  const uint32_t packed_parent = lo4 & 0x00FF'FFFFu;
  const uint32_t name = (lo4 >> 24) | (static_cast<uint32_t>(hi2) << 8);
  return PathNode{
      packed_parent == kMaxPackedPathCount ? kRootPath : packed_parent, name};
}

// -- Postings pool: delta + varint --
//
// Postings for a given basename are always written sorted ascending by
// (pkg, path) (see DbBuilder::WriteToFile), and the pool is only ever read
// as one small bounded slice at a time -- located by a binary search on the
// (fixed-width) basename index, never itself binary-searched -- so
// variable-length encoding here costs nothing beyond decoding the handful
// of postings a lookup actually matched (a real repo averages ~8 per
// pooled basename).
//
// Standard unsigned LEB128 varints. Each posting is two varints:
//   - pkg delta from the previous posting's pkg (or from 0, for the first
//     posting in a slice).
//   - if that pkg is unchanged from the previous posting, the path delta
//     from the previous posting's path; otherwise the path itself,
//     absolute (a path from a different package isn't necessarily anywhere
//     near the last one, so a delta would just as often inflate as shrink).

inline void AppendVarint(std::string* buf, uint64_t value) {
  while (value >= 0x80) {
    buf->push_back(static_cast<char>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  buf->push_back(static_cast<char>(value));
}

// Reads one varint from `data[*pos, size)`, advancing *pos past it on
// success. Returns false (leaving *pos and *out unspecified) on truncation
// or an encoding that would need more than 64 bits -- both are signs of a
// corrupt file, since every varint this format ever writes fits in a
// handful of bytes.
inline bool ReadVarint(const uint8_t* data, size_t size, size_t* pos,
                       uint64_t* out) {
  uint64_t result = 0;
  int shift = 0;
  for (;;) {
    if (*pos >= size || shift >= 64) {
      return false;
    }
    const uint8_t byte = data[(*pos)++];
    result |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) {
      *out = result;
      return true;
    }
    shift += 7;
  }
}

// Appends `postings` (already sorted ascending by (pkg, path)) to `buf` in
// the delta+varint wire format described above.
inline void EncodePostingsDelta(std::string* buf,
                                std::span<const Posting> postings) {
  uint32_t prev_pkg = 0;
  uint32_t prev_path = 0;
  for (const Posting& p : postings) {
    AppendVarint(buf, static_cast<uint64_t>(p.pkg) - prev_pkg);
    if (p.pkg == prev_pkg) {
      AppendVarint(buf, static_cast<uint64_t>(p.path) - prev_path);
    } else {
      AppendVarint(buf, p.path);
    }
    prev_pkg = p.pkg;
    prev_path = p.path;
  }
}

// Decodes `count` postings starting at byte `start` of `blob[0, blob_size)`
// (the postings pool) into `*out` (cleared first, then filled in order).
// Returns false -- leaving `*out` in an unspecified state -- if `start` is
// out of range, `count` couldn't possibly fit in the bytes remaining (each
// posting needs at least 2 bytes, so this also bounds a corrupt/hostile
// count before it can drive an oversized allocation), a varint is
// truncated, or a decoded pkg/path would overflow 32 bits. Callers must
// check the return value rather than assume decoded content is complete;
// this only validates the encoding is well-formed, not that the decoded
// pkg/path values are in range for this repo's tables -- that's on the
// caller, same as for the inline-posting case.
inline bool DecodePostingsDelta(const uint8_t* blob, uint64_t blob_size,
                                uint64_t start, uint32_t count,
                                std::vector<Posting>* out) {
  out->clear();
  if (start > blob_size) {
    return false;
  }
  const size_t size = static_cast<size_t>(blob_size);
  size_t pos = static_cast<size_t>(start);

  const uint64_t max_possible_count = (size - pos) / 2;
  if (count > max_possible_count) {
    return false;
  }
  out->reserve(count);

  uint32_t prev_pkg = 0;
  uint32_t prev_path = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t pkg_delta;
    if (!ReadVarint(blob, size, &pos, &pkg_delta)) {
      return false;
    }
    const uint64_t pkg = static_cast<uint64_t>(prev_pkg) + pkg_delta;
    if (pkg > UINT32_MAX) {
      return false;
    }

    uint64_t second;
    if (!ReadVarint(blob, size, &pos, &second)) {
      return false;
    }
    const uint64_t path =
        (pkg == prev_pkg) ? static_cast<uint64_t>(prev_path) + second : second;
    if (path > UINT32_MAX) {
      return false;
    }

    out->push_back(
        Posting{static_cast<uint32_t>(pkg), static_cast<uint32_t>(path)});
    prev_pkg = static_cast<uint32_t>(pkg);
    prev_path = static_cast<uint32_t>(path);
  }
  return true;
}

#pragma pack(push, 1)

struct Header {
  char magic[4];  // "PFDB"
  uint32_t version;
  uint32_t byte_order_guard;  // written/checked as 0x01020304
  uint32_t reserved;

  uint32_t repo_name;  // StringId
  uint32_t package_count;

  uint64_t byte_pool_offset, byte_pool_size;
  // string_table_count is the number of distinct strings; the table itself
  // physically holds string_table_count+1 uint32_t byte-pool offsets (one
  // past the last string's start, as a sentinel), so that a string's length
  // is always offset[id+1]-offset[id] without a separate length field.
  uint64_t string_table_offset, string_table_count;
  // path_table_count is measured in kPackedPathNodeSize-byte packed nodes,
  // not sizeof(PathNode).
  uint64_t path_table_offset, path_table_count;
  uint64_t package_table_offset, package_table_count;
  uint64_t package_files_offset, package_files_count;
  uint64_t basename_index_offset, basename_index_count;
  // postings_count is the byte length of the delta+varint-encoded postings
  // blob at postings_offset, not a count of Posting entries -- each
  // BasenameEntry carries its own decode count (see PostingCountOf).
  uint64_t postings_offset, postings_count;
};
static_assert(sizeof(Header) % 8 == 0);

#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<PathNode>);
static_assert(std::is_trivially_copyable_v<Package>);
static_assert(std::is_trivially_copyable_v<BasenameEntry>);
static_assert(std::is_trivially_copyable_v<Posting>);
static_assert(std::is_trivially_copyable_v<Header>);

inline constexpr char kMagic[4] = {'P', 'F', 'D', 'B'};
inline constexpr uint32_t kVersion = 2;
inline constexpr uint32_t kByteOrderGuard = 0x01020304u;

// Rounds `n` up to the next multiple of `align` (align must be a power of 2).
inline constexpr size_t AlignUp(size_t n, size_t align) {
  return (n + align - 1) & ~(align - 1);
}

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
