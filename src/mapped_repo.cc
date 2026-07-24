#include "mapped_repo.hh"

#include <sys/mman.h>

#include <algorithm>
#include <cstring>

namespace pkgfile::db {

namespace {

// Returns true if [offset, offset+count*sizeof(T)) fits within a file of
// `file_size` bytes, guarding every mmap'd access below against a truncated
// or corrupt database file.
template <typename T>
bool FitsWithin(uint64_t offset, uint64_t count, uint64_t file_size) {
  if (offset > file_size) {
    return false;
  }
  const uint64_t bytes = count * sizeof(T);
  return bytes / sizeof(T) == count &&  // overflow check
         file_size - offset >= bytes;
}

}  // namespace

// static
std::unique_ptr<MappedRepo> MappedRepo::Open(const std::string& path,
                                             OpenError* error) {
  auto file = ReadOnlyFile::Open(path, /*try_mmap=*/true);
  if (file == nullptr) {
    *error = OpenError::kFileError;
    return nullptr;
  }

  const auto& mapped = file->mmapped();
  if (!mapped) {
    *error = OpenError::kNotMapped;
    return nullptr;
  }

  // No madvise() here: the right access-pattern hint depends on which query
  // strategy ends up using this repo (an indexed lookup vs. a full scan
  // want opposite advice -- see AdviseRandomAccess()/AdviseSequentialAccess()
  // below), and that isn't known yet at open time. Callers advise once they
  // know.

  const uint64_t file_size = static_cast<uint64_t>(mapped->size);
  if (file_size < sizeof(Header)) {
    *error = OpenError::kTooSmall;
    return nullptr;
  }

  const auto* header = static_cast<const Header*>(mapped->ptr);
  if (memcmp(header->magic, kMagic, sizeof(kMagic)) != 0) {
    *error = OpenError::kBadMagic;
    return nullptr;
  }
  if (header->version != kVersion) {
    *error = OpenError::kWrongVersion;
    return nullptr;
  }
  if (header->byte_order_guard != kByteOrderGuard) {
    *error = OpenError::kWrongByteOrder;
    return nullptr;
  }

  if (!FitsWithin<char>(header->byte_pool_offset, header->byte_pool_size,
                        file_size) ||
      !FitsWithin<StringRef>(header->string_table_offset,
                             header->string_table_count, file_size) ||
      !FitsWithin<PathNode>(header->path_table_offset, header->path_table_count,
                            file_size) ||
      !FitsWithin<Package>(header->package_table_offset,
                           header->package_table_count, file_size) ||
      !FitsWithin<uint32_t>(header->package_files_offset,
                            header->package_files_count, file_size) ||
      !FitsWithin<BasenameEntry>(header->basename_index_offset,
                                 header->basename_index_count, file_size) ||
      !FitsWithin<Posting>(header->postings_offset, header->postings_count,
                           file_size)) {
    *error = OpenError::kTruncated;
    return nullptr;
  }

  return std::unique_ptr<MappedRepo>(new MappedRepo(std::move(file), header));
}

void MappedRepo::AdviseRandomAccess() const {
  const auto& mapped = file_->mmapped();
  madvise(mapped->ptr, mapped->size, MADV_RANDOM);
}

void MappedRepo::AdviseSequentialAccess() const {
  const auto& mapped = file_->mmapped();
  madvise(mapped->ptr, mapped->size, MADV_SEQUENTIAL);
}

std::string_view MappedRepo::ResolveString(StringId id) const {
  if (id >= header_->string_table_count) {
    return {};
  }
  const StringRef ref = PtrAt<StringRef>(header_->string_table_offset)[id];
  // offset/length come straight from the file; check as uint64_t so a
  // corrupt pair summing past UINT32_MAX can't wrap back into range.
  if (uint64_t{ref.offset} + ref.length > header_->byte_pool_size) {
    return {};
  }
  return {reinterpret_cast<const char*>(PtrAt<char>(header_->byte_pool_offset) +
                                        ref.offset),
          ref.length};
}

namespace {
// Real package file lists never come close to this; it's here so a
// pathologically deep path (a vendored node_modules tree, say) degrades to
// a second trie walk instead of either overflowing a fixed buffer or making
// every call pay for a heap-allocated one.
constexpr size_t kMaxInlineDepth = 128;
}  // namespace

void MappedRepo::ResolvePathInto(uint32_t tagged_path, std::string* out,
                                 PathCache* cache) const {
  const bool is_dir = IsDirOf(tagged_path);
  const PathId leaf = PathIdOf(tagged_path);
  const PathNode& leaf_node = PathNodeAt(leaf);

  // Fast path: the previous call in this cache resolved a file with the
  // same immediate parent. Every ancestor beyond that parent is therefore
  // identical too, so skip straight to gluing the leaf's own name onto the
  // parent's already-resolved prefix instead of re-walking the trie.
  if (cache != nullptr && cache->valid && leaf_node.parent == cache->parent) {
    const std::string_view name = ResolveString(leaf_node.name);
    out->assign(cache->prefix);
    out->append(name);
    if (is_dir) {
      out->push_back('/');
    }
    return;
  }

  // Single walk of the trie leaf-to-root: record each component in a
  // stack-resident array (no allocation) while accumulating the exact
  // output length, so the write pass below doesn't need to walk the trie
  // a second time to rediscover the same components.
  std::string_view components[kMaxInlineDepth];
  size_t depth = 0;
  size_t length = is_dir ? 1 : 0;
  bool overflowed = false;

  const PathNode* node = &leaf_node;
  for (PathId id = leaf;;) {
    const std::string_view name = ResolveString(node->name);
    length += 1 + name.size();
    if (depth < kMaxInlineDepth) {
      components[depth] = name;
    } else {
      overflowed = true;
    }
    ++depth;

    id = node->parent;
    if (id == kRootPath) {
      break;
    }
    node = &PathNodeAt(id);
  }

  if (overflowed) {
    ResolvePathIntoSlow(tagged_path, length, out);
  } else {
    // resize_and_overwrite hands us an uninitialized buffer of exactly
    // `length` bytes rather than value-initializing it the way resize()
    // would, which matters here since every one of those bytes is about to
    // be overwritten anyway.
    out->resize_and_overwrite(length, [&](char* buf, size_t n) {
      size_t pos = n;
      if (is_dir) {
        buf[--pos] = '/';
      }
      // `components` was filled leaf-to-root (components[0] is the leaf);
      // the write position walks backward from the end of the buffer as
      // each component is consumed, so processing them in that same
      // leaf-to-root order here lands the leaf nearest the end and the
      // top-level component nearest the start -- exactly like the direct
      // trie walk this array replaced.
      for (size_t i = 0; i < depth; ++i) {
        const std::string_view name = components[i];
        pos -= name.size();
        std::char_traits<char>::copy(buf + pos, name.data(), name.size());
        buf[--pos] = '/';
      }
      return n;
    });
  }

  if (cache != nullptr && !overflowed) {
    // Everything in `out` except the leaf's own name (and its trailing
    // slash, if this occurrence is a directory) is exactly the parent's own
    // resolved-as-a-directory path -- reuse it verbatim rather than paying
    // for a second walk up to the parent.
    const std::string_view leaf_name = components[0];
    const size_t strip = leaf_name.size() + (is_dir ? 1 : 0);
    cache->prefix.assign(*out, 0, out->size() - strip);
    cache->parent = leaf_node.parent;
    cache->valid = true;
  }
}

void MappedRepo::ResolvePathIntoSlow(uint32_t tagged_path, size_t length,
                                     std::string* out) const {
  const bool is_dir = IsDirOf(tagged_path);
  const PathId leaf = PathIdOf(tagged_path);

  out->resize_and_overwrite(length, [&](char* buf, size_t n) {
    size_t pos = n;
    if (is_dir) {
      buf[--pos] = '/';
    }
    for (PathId id = leaf;;) {
      const PathNode& node = PathNodeAt(id);
      const std::string_view name = ResolveString(node.name);
      pos -= name.size();
      std::char_traits<char>::copy(buf + pos, name.data(), name.size());
      buf[--pos] = '/';

      id = node.parent;
      if (id == kRootPath) {
        break;
      }
    }
    return n;
  });
}

const Package* MappedRepo::FindPackageByName(std::string_view name) const {
  const auto pkgs = packages();
  const auto iter =
      std::lower_bound(pkgs.begin(), pkgs.end(), name,
                       [&](const Package& pkg, std::string_view target) {
                         return ResolveString(pkg.name) < target;
                       });

  if (iter == pkgs.end() || ResolveString(iter->name) != name) {
    return nullptr;
  }
  return &*iter;
}

std::span<const Posting> MappedRepo::PostingsFor(const BasenameEntry& entry,
                                                 Posting* single) const {
  if (HasInlinePosting(entry)) {
    const PkgId pkg = InlinePkgOf(entry);
    const PathId path = PathIdOf(InlineTaggedPathOf(entry));
    if (pkg >= header_->package_table_count ||
        path >= header_->path_table_count) {
      return {};
    }
    *single = Posting{pkg, InlineTaggedPathOf(entry)};
    return {single, 1};
  }

  const uint64_t table_count = header_->postings_count;
  const uint64_t start = std::min<uint64_t>(entry.postings_start, table_count);
  const uint64_t count =
      std::min<uint64_t>(entry.postings_count, table_count - start);
  const Posting* postings = PtrAt<Posting>(header_->postings_offset) + start;

  for (uint64_t i = 0; i < count; ++i) {
    if (postings[i].pkg >= header_->package_table_count ||
        PathIdOf(postings[i].path) >= header_->path_table_count) {
      return {};
    }
  }
  return {postings, count};
}

const BasenameEntry* MappedRepo::FindBasename(std::string_view name) const {
  const auto entries = basename_index();
  const auto iter = std::lower_bound(
      entries.begin(), entries.end(), name,
      [&](const BasenameEntry& entry, std::string_view target) {
        return ResolveString(entry.name) < target;
      });

  if (iter == entries.end() || ResolveString(iter->name) != name) {
    return nullptr;
  }
  return &*iter;
}

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
