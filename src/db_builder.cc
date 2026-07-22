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

  return std::make_pair(entryname.substr(0, pkgver),
                        entryname.substr(pkgver + 1));
}

// Appends raw bytes of `value` to `buf`, then pads `buf` out to `align` bytes.
template <typename T>
void AppendAligned(std::string* buf, const T* data, size_t count,
                   size_t align) {
  if (count > 0) {
    buf->append(reinterpret_cast<const char*>(data), count * sizeof(T));
  }
  buf->resize(AlignUp(buf->size(), align), '\0');
}

}  // namespace

DbBuilder::DbBuilder(std::string reponame) : reponame_(std::move(reponame)) {}

StringId DbBuilder::InternString(std::string_view s) {
  if (auto iter = string_lookup_.find(std::string(s));
      iter != string_lookup_.end()) {
    return iter->second;
  }

  const StringId id = static_cast<StringId>(strings_.size());
  strings_.emplace_back(s);
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

    if (auto iter = path_lookup_.find(key); iter != path_lookup_.end()) {
      parent = iter->second;
    } else {
      const PathId this_id = static_cast<PathId>(paths_.size());
      paths_.push_back(PathNode{parent, name_id});
      path_lookup_.emplace(key, this_id);
      parent = this_id;
    }

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

    basename_postings_[paths_[leaf].name].push_back(Posting{raw_index, tagged});
  }

  packages_.push_back(std::move(pkg));
}

// static
std::unique_ptr<DbBuilder> DbBuilder::FromArchive(std::string reponame,
                                                  int fd_in,
                                                  const char** error) {
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
  }

  return builder;
}

bool DbBuilder::WriteToFile(const std::string& path, int64_t mtime) {
  const StringId repo_name_id = InternString(reponame_);

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

  // Sort distinct basenames by their text so the index can be binary
  // searched, remapping postings to final PkgIds along the way.
  std::vector<StringId> basenames;
  basenames.reserve(basename_postings_.size());
  for (const auto& [id, postings] : basename_postings_) {
    basenames.push_back(id);
  }
  std::sort(basenames.begin(), basenames.end(),
            [&](StringId a, StringId b) { return strings_[a] < strings_[b]; });

  std::vector<BasenameEntry> basename_index;
  std::vector<Posting> postings_pool;
  basename_index.reserve(basenames.size());

  for (const StringId basename_id : basenames) {
    std::vector<Posting> postings = basename_postings_[basename_id];
    for (Posting& p : postings) {
      p.pkg = old_to_new[p.pkg];
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

  // Byte pool + string table, in original StringId order.
  std::string byte_pool;
  std::vector<StringRef> string_table;
  string_table.reserve(strings_.size());
  for (const auto& s : strings_) {
    string_table.push_back(StringRef{static_cast<uint32_t>(byte_pool.size()),
                                     static_cast<uint32_t>(s.size())});
    byte_pool += s;
  }

  std::string buf;
  buf.resize(sizeof(Header), '\0');

  Header header{};
  memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kVersion;
  header.byte_order_guard = kByteOrderGuard;
  header.repo_name = repo_name_id;
  header.package_count = static_cast<uint32_t>(package_table.size());

  header.byte_pool_offset = buf.size();
  header.byte_pool_size = byte_pool.size();
  AppendAligned(&buf, byte_pool.data(), byte_pool.size(), 8);

  header.string_table_offset = buf.size();
  header.string_table_count = string_table.size();
  AppendAligned(&buf, string_table.data(), string_table.size(), 8);

  header.path_table_offset = buf.size();
  header.path_table_count = paths_.size();
  AppendAligned(&buf, paths_.data(), paths_.size(), 8);

  header.package_table_offset = buf.size();
  header.package_table_count = package_table.size();
  AppendAligned(&buf, package_table.data(), package_table.size(), 8);

  header.package_files_offset = buf.size();
  header.package_files_count = package_files_pool.size();
  AppendAligned(&buf, package_files_pool.data(), package_files_pool.size(), 8);

  header.basename_index_offset = buf.size();
  header.basename_index_count = basename_index.size();
  AppendAligned(&buf, basename_index.data(), basename_index.size(), 8);

  header.postings_offset = buf.size();
  header.postings_count = postings_pool.size();
  AppendAligned(&buf, postings_pool.data(), postings_pool.size(), 8);

  buf.replace(0, sizeof(Header), reinterpret_cast<const char*>(&header),
              sizeof(Header));

  const std::string tmppath = path + "~";
  const int fd = open(tmppath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    std::cerr << std::format("error: failed to open {} for writing: {}\n",
                             tmppath, strerror(errno));
    return false;
  }

  const char* data = buf.data();
  size_t remaining = buf.size();
  while (remaining > 0) {
    const ssize_t n = write(fd, data, remaining);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << std::format("error: failed to write {}: {}\n", tmppath,
                               strerror(errno));
      close(fd);
      unlink(tmppath.c_str());
      return false;
    }
    data += n;
    remaining -= n;
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
