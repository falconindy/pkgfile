#include "db_builder.hh"

#include <archive_entry.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <numeric>
#include <tuple>

#include "archive_io.hh"
#include "archive_reader.hh"

namespace fs = std::filesystem;

namespace pkgfile::db {

namespace {

// Parses "$pkgname-$pkgver-$pkgrel" into ($pkgname, $pkgver-$pkgrel), the same
// split pkgfile has always used at query time. Returns nullopt if there
// aren't at least two hyphens to split on.
std::optional<std::pair<std::string_view, std::string_view>>
ParsePkgNameVersion(std::string_view entryname) {
  const auto pkgrel = entryname.rfind('-');
  if (pkgrel == entryname.npos) {
    return std::nullopt;
  }

  const auto pkgver = entryname.substr(0, pkgrel).rfind('-');
  if (pkgver == entryname.npos) {
    return std::nullopt;
  }

  return std::pair(entryname.substr(0, pkgver), entryname.substr(pkgver + 1));
}

// Writes every byte of `data`, retrying on EINTR/short writes.
bool WriteAll(int fd, const char* data, size_t remaining) {
  while (remaining > 0) {
    const ssize_t n = write(fd, data, remaining);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    data += n;
    remaining -= n;
  }
  return true;
}

}  // namespace

DbBuilder::DbBuilder(std::string reponame) : reponame_(std::move(reponame)) {}

StringId DbBuilder::InternString(std::string_view s) {
  if (auto iter = string_lookup_.find(s); iter != string_lookup_.end()) {
    return iter->second;
  }

  const StringId id = static_cast<StringId>(strings_.size());
  strings_.emplace_back(s);
  // strings_ is a deque, so this view into the just-inserted element stays
  // valid for the string's lifetime even as strings_ keeps growing.
  string_lookup_.emplace(strings_.back(), id);
  return id;
}

PathId DbBuilder::InternPath(std::string_view path) {
  PathId parent = kRootPath;

  size_t start = 0;
  while (start <= path.size()) {
    const auto slash = path.find('/', start);
    const std::string_view component = slash == path.npos
                                           ? path.substr(start)
                                           : path.substr(start, slash - start);

    const StringId name_id = InternString(component);
    const uint64_t key = (static_cast<uint64_t>(parent) << 32) | name_id;

    parent = path_lookup_.GetOrInsert(key, [&] {
      const PathId this_id = static_cast<PathId>(paths_.size());
      paths_.push_back(PathNode{parent, name_id});
      return this_id;
    });

    if (slash == path.npos) {
      break;
    }
    start = slash + 1;
  }

  return parent;
}

void DbBuilder::AddPackage(
    std::string_view name, std::string_view version,
    const std::vector<std::pair<std::string, bool>>& files) {
  const uint32_t raw_index = static_cast<uint32_t>(packages_.size());

  PendingPackage pkg;
  pkg.name = InternString(name);
  pkg.version = InternString(version);
  pkg.tagged_files.reserve(files.size());

  for (const auto& [path, is_dir] : files) {
    const PathId leaf = InternPath(path);
    const uint32_t tagged = TagPath(leaf, is_dir);
    pkg.tagged_files.push_back(tagged);

    const auto iter =
        basename_heads_.try_emplace(paths_[leaf].name, kNoPosting).first;
    pending_postings_.push_back(
        PendingPosting{Posting{raw_index, tagged}, iter->second});
    iter->second = static_cast<uint32_t>(pending_postings_.size() - 1);
  }

  packages_.push_back(std::move(pkg));
}

// static
std::unique_ptr<DbBuilder> DbBuilder::FromArchive(
    std::string reponame, int fd_in, const char** error,
    const std::function<void(int64_t bytes_read)>& on_progress) {
  auto reader = ReadArchive::New(fd_in, error);
  if (reader == nullptr) {
    return nullptr;
  }

  auto builder = std::make_unique<DbBuilder>(std::move(reponame));

  archive_entry* ae;
  while (archive_read_next_header(reader->read_archive(), &ae) == ARCHIVE_OK) {
    const fs::path entryname = archive_entry_pathname(ae);
    if (entryname.filename() != "files") {
      continue;
    }

    // Named so it outlives this iteration: ParsePkgNameVersion returns
    // string_views into it, which AddPackage() reads below.
    const std::string entry_stem = entryname.parent_path().native();

    const auto name_version = ParsePkgNameVersion(entry_stem);
    if (!name_version) {
      std::cerr << std::format("error parsing pkgname from: {}\n", entry_stem);
      continue;
    }
    const auto [name, version] = *name_version;

    ArchiveReader entry_reader(reader->read_archive());
    std::string_view line;

    // discard the "%FILES%" header line
    entry_reader.GetLine(&line);

    std::vector<std::pair<std::string, bool>> files;
    while (entry_reader.GetLine(&line) == ARCHIVE_OK) {
      const bool is_dir = line.ends_with('/');
      files.emplace_back(
          is_dir ? line.substr(0, line.size() - 1) : std::string(line), is_dir);
    }

    builder->AddPackage(name, version, files);

    if (on_progress) {
      on_progress(archive_filter_bytes(reader->read_archive(), -1));
    }
  }

  return builder;
}

bool DbBuilder::WriteToFile(const std::string& path, int64_t mtime) {
  const StringId repo_name_id = InternString(reponame_);

  // Build-time-only indices: everything below reads the data they pointed
  // into (strings_, paths_), never the indices themselves, so drop them now
  // rather than carrying their hash tables past the last point they're used.
  std::unordered_map<std::string_view, StringId>().swap(string_lookup_);
  path_lookup_ = PathIndex();

  // Sort packages by name, and remap every reference to a package's original
  // (insertion-order) index to its new, sorted PkgId so that list mode can
  // binary search the package table.
  std::vector<uint32_t> order(packages_.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    return strings_[packages_[a].name] < strings_[packages_[b].name];
  });

  std::vector<uint32_t> old_to_new(packages_.size());
  for (uint32_t new_idx = 0; new_idx < order.size(); ++new_idx) {
    old_to_new[order[new_idx]] = new_idx;
  }

  std::vector<Package> package_table;
  std::vector<uint32_t> package_files_pool;
  package_table.reserve(packages_.size());

  for (const uint32_t old_idx : order) {
    const PendingPackage& pkg = packages_[old_idx];

    package_table.push_back(Package{
        pkg.name,
        pkg.version,
        static_cast<uint32_t>(package_files_pool.size()),
        static_cast<uint32_t>(pkg.tagged_files.size()),
    });

    package_files_pool.insert(package_files_pool.end(),
                              pkg.tagged_files.begin(), pkg.tagged_files.end());
  }
  std::vector<PendingPackage>().swap(packages_);

  // Sort distinct basenames by their text so the index can be binary
  // searched, remapping postings to final PkgIds along the way.
  std::vector<StringId> basenames;
  basenames.reserve(basename_heads_.size());
  for (const auto& [id, head] : basename_heads_) {
    basenames.push_back(id);
  }
  std::sort(basenames.begin(), basenames.end(),
            [&](StringId a, StringId b) { return strings_[a] < strings_[b]; });

  std::vector<BasenameEntry> basename_index;
  std::vector<Posting> postings_pool;
  basename_index.reserve(basenames.size());

  // Reused across every basename instead of a fresh heap allocation per one:
  // capped by the largest number of occurrences any single basename has.
  std::vector<Posting> postings;
  for (const StringId basename_id : basenames) {
    postings.clear();
    for (uint32_t idx = basename_heads_[basename_id]; idx != kNoPosting;
         idx = pending_postings_[idx].prev) {
      Posting p = pending_postings_[idx].posting;
      p.pkg = old_to_new[p.pkg];
      postings.push_back(p);
    }
    std::sort(postings.begin(), postings.end(),
              [](const Posting& a, const Posting& b) {
                return std::tie(a.pkg, a.path) < std::tie(b.pkg, b.path);
              });

    if (postings.size() == 1) {
      // The overwhelmingly common case: inline the single occurrence into
      // the entry itself rather than spending a whole Posting (and a
      // pointer chase to reach it) in the pool. See db_format.hh.
      basename_index.push_back(BasenameEntry{
          basename_id,
          postings[0].pkg | kInlinePostingBit,
          postings[0].path,
      });
    } else {
      basename_index.push_back(BasenameEntry{
          basename_id,
          static_cast<uint32_t>(postings_pool.size()),
          static_cast<uint32_t>(postings.size()),
      });
      postings_pool.insert(postings_pool.end(), postings.begin(),
                           postings.end());
    }
  }
  std::vector<PendingPosting>().swap(pending_postings_);
  std::unordered_map<StringId, uint32_t>().swap(basename_heads_);

  const std::string tmppath = path + "~";
  const int fd = open(tmppath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    std::cerr << std::format("error: failed to open {} for writing: {}\n",
                             tmppath, strerror(errno));
    return false;
  }

  auto fail = [&] {
    std::cerr << std::format("error: failed to write {}: {}\n", tmppath,
                             strerror(errno));
    close(fd);
    unlink(tmppath.c_str());
    return false;
  };

  Header header{};
  memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kVersion;
  header.byte_order_guard = kByteOrderGuard;
  header.repo_name = repo_name_id;
  header.package_count = static_cast<uint32_t>(package_table.size());

  // The header goes at the very start of the file but isn't known in full
  // (it holds every section's offset) until every section below has been
  // written, so reserve its space now and come back to fill it in once
  // `header` itself is complete.
  if (!WriteAll(fd, reinterpret_cast<const char*>(&header), sizeof(header))) {
    return fail();
  }

  size_t offset = sizeof(Header);
  static constexpr char kZeros[8] = {};

  // Writes `count * sizeof(T)` bytes of `data`, records the section's offset
  // and count into `header`'s `*_offset`/`*_count` fields, then zero-pads out
  // to 8 bytes so every section (all of which are POD arrays) stays naturally
  // aligned for the mmap'd reader.
  auto write_section = [&](uint64_t& header_offset, uint64_t& header_count,
                           const auto* data, size_t count) {
    using T = std::remove_pointer_t<decltype(data)>;
    header_offset = offset;
    header_count = count;

    const size_t bytes = count * sizeof(T);
    if (bytes > 0 &&
        !WriteAll(fd, reinterpret_cast<const char*>(data), bytes)) {
      return false;
    }
    const size_t pad = AlignUp(bytes, 8) - bytes;
    if (pad > 0 && !WriteAll(fd, kZeros, pad)) {
      return false;
    }
    offset += bytes + pad;
    return true;
  };

  // Byte pool + string table, in original StringId order. Written directly
  // from strings_ rather than concatenated into an intermediate buffer
  // first, so peak memory never holds a second full copy of every interned
  // string's bytes.
  std::vector<StringRef> string_table;
  string_table.reserve(strings_.size());

  header.byte_pool_offset = offset;
  size_t pool_size = 0;
  for (const auto& s : strings_) {
    string_table.push_back(StringRef{static_cast<uint32_t>(pool_size),
                                     static_cast<uint32_t>(s.size())});
    if (!s.empty() && !WriteAll(fd, s.data(), s.size())) {
      return fail();
    }
    pool_size += s.size();
  }
  header.byte_pool_size = pool_size;
  const size_t pool_pad = AlignUp(pool_size, 8) - pool_size;
  if (pool_pad > 0 && !WriteAll(fd, kZeros, pool_pad)) {
    return fail();
  }
  offset += pool_size + pool_pad;
  decltype(strings_)().swap(strings_);

  if (!write_section(header.string_table_offset, header.string_table_count,
                     string_table.data(), string_table.size()) ||
      !write_section(header.path_table_offset, header.path_table_count,
                     paths_.data(), paths_.size()) ||
      !write_section(header.package_table_offset, header.package_table_count,
                     package_table.data(), package_table.size()) ||
      !write_section(header.package_files_offset, header.package_files_count,
                     package_files_pool.data(), package_files_pool.size()) ||
      !write_section(header.basename_index_offset, header.basename_index_count,
                     basename_index.data(), basename_index.size()) ||
      !write_section(header.postings_offset, header.postings_count,
                     postings_pool.data(), postings_pool.size())) {
    return fail();
  }

  if (lseek(fd, 0, SEEK_SET) != 0 ||
      !WriteAll(fd, reinterpret_cast<const char*>(&header), sizeof(header))) {
    return fail();
  }

  const struct timeval times[2] = {
      {mtime, 0},
      {mtime, 0},
  };
  futimes(fd, times);

  close(fd);

  std::error_code ec;
  fs::rename(tmppath, path, ec);
  if (ec.value() != 0) {
    std::cerr << std::format("error: renaming {} to {} failed: {}\n", tmppath,
                             path, ec.message());
    return false;
  }

  return true;
}

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
