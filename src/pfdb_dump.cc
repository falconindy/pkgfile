// pfdb-dump: a debugging aid for inspecting the pkgfile repo database
// format ("PFDB", see db_format.hh). Not installed, not covered by any
// compatibility guarantee -- just a window into what a .files db actually
// contains, for use while developing or diagnosing pkgfile itself.

#include <sys/stat.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "db_format.hh"
#include "mapped_repo.hh"

namespace fs = std::filesystem;
using pkgfile::db::BasenameEntry;
using pkgfile::db::MappedRepo;
using pkgfile::db::Package;
using pkgfile::db::PathNode;
using pkgfile::db::Posting;

namespace {

const char* OpenErrorToString(MappedRepo::OpenError error) {
  switch (error) {
    case MappedRepo::OpenError::kFileError:
      return "failed to open or stat the file";
    case MappedRepo::OpenError::kNotMapped:
      return "failed to mmap the file";
    case MappedRepo::OpenError::kTooSmall:
      return "file is too small to contain a header";
    case MappedRepo::OpenError::kBadMagic:
      return "bad magic (not a PFDB file?)";
    case MappedRepo::OpenError::kWrongVersion:
      return "unsupported format version";
    case MappedRepo::OpenError::kWrongByteOrder:
      return "byte-order guard mismatch";
    case MappedRepo::OpenError::kTruncated:
      return "a section falls outside the file (truncated?)";
  }
  return "unknown error";
}

std::string FormatTime(int64_t unix_time) {
  const time_t t = static_cast<time_t>(unix_time);
  char buf[32];
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);
  return buf;
}

void PrintSizeRow(const char* label, uint64_t count, uint64_t bytes,
                  uint64_t total_bytes) {
  const double pct = total_bytes == 0 ? 0.0 : 100.0 * bytes / total_bytes;
  std::cout << std::format("  {:<16} {:>10}  {:>12} bytes  ({:5.1f}%)\n", label,
                           count, bytes, pct);
}

// Default (no subcommand): header fields, per-section size breakdown, and a
// couple of derived compaction stats.
void CmdSummary(const MappedRepo& repo, uint64_t file_size, int64_t mtime) {
  uint64_t total_files = 0;
  for (const auto& pkg : repo.packages()) {
    total_files += pkg.files_count;
  }

  uint64_t total_postings = 0;
  uint64_t inlined_postings = 0;
  for (const auto& entry : repo.basename_index()) {
    total_postings += pkgfile::db::PostingCountOf(entry);
    if (pkgfile::db::HasInlinePosting(entry)) {
      ++inlined_postings;
    }
  }

  std::cout << std::format("repo:          {}\n", repo.reponame());
  std::cout << std::format("mtime:         {} ({})\n", mtime,
                           FormatTime(mtime));
  std::cout << std::format("file size:     {} bytes\n\n", file_size);

  std::cout << "sections:\n";
  std::cout << std::format("  {:<16} {:>10}  {:>12}\n", "", "count", "bytes");
  PrintSizeRow("byte pool", repo.string_count(), repo.byte_pool_size(),
               file_size);
  PrintSizeRow("string table", repo.string_count(),
               repo.string_count() * sizeof(pkgfile::db::StringRef), file_size);
  PrintSizeRow("path table", repo.path_count(),
               repo.path_count() * sizeof(PathNode), file_size);
  PrintSizeRow("package table", repo.packages().size(),
               repo.packages().size() * sizeof(Package), file_size);
  PrintSizeRow("package files", total_files, total_files * sizeof(uint32_t),
               file_size);
  PrintSizeRow("basename index", repo.basename_index().size(),
               repo.basename_index().size() * sizeof(BasenameEntry), file_size);
  const uint64_t stored_postings = total_postings - inlined_postings;
  PrintSizeRow("postings", stored_postings, stored_postings * sizeof(Posting),
               file_size);

  std::cout << std::format(
      "\n{} packages, {} total file entries, {} distinct paths, {} distinct "
      "basenames\n",
      repo.packages().size(), total_files, repo.path_count(),
      repo.basename_index().size());

  if (repo.path_count() > 0) {
    std::cout << std::format(
        "average {:.2f} file entries per distinct path (higher means more "
        "sharing from interning)\n",
        static_cast<double>(total_files) / repo.path_count());
  }

  if (repo.basename_index().size() > 0) {
    std::cout << std::format(
        "{} of {} basenames ({:.1f}%) have a single occurrence and are "
        "inlined into the index, saving {} bytes that would otherwise sit "
        "in the postings pool\n",
        inlined_postings, repo.basename_index().size(),
        100.0 * inlined_postings / repo.basename_index().size(),
        inlined_postings * sizeof(Posting));
  }
}

void CmdPackages(const MappedRepo& repo) {
  std::cout << std::format("{:<40} {:<20} {:>8}\n", "name", "version", "files");
  for (const auto& pkg : repo.packages()) {
    std::cout << std::format("{:<40} {:<20} {:>8}\n",
                             repo.ResolveString(pkg.name),
                             repo.ResolveString(pkg.version), pkg.files_count);
  }
}

int CmdFiles(const MappedRepo& repo, std::string_view pkgname) {
  const Package* pkg = repo.FindPackageByName(pkgname);
  if (pkg == nullptr) {
    std::cerr << std::format("error: no package named '{}' in this repo\n",
                             pkgname);
    return 1;
  }

  for (const auto tagged_path : repo.PackageFiles(*pkg)) {
    std::cout << (pkgfile::db::IsDirOf(tagged_path) ? "d " : "f ")
              << repo.ResolvePath(tagged_path) << "\n";
  }
  return 0;
}

void CmdBasenames(const MappedRepo& repo) {
  std::vector<const BasenameEntry*> entries;
  entries.reserve(repo.basename_index().size());
  for (const auto& entry : repo.basename_index()) {
    entries.push_back(&entry);
  }

  // Most-referenced first: the natural question when looking at this list is
  // "what's actually taking up space/traffic in the index".
  std::sort(entries.begin(), entries.end(),
            [](const BasenameEntry* a, const BasenameEntry* b) {
              return pkgfile::db::PostingCountOf(*a) >
                     pkgfile::db::PostingCountOf(*b);
            });

  std::cout << std::format("{:>10}  {:<7}  {}\n", "postings", "inline",
                           "basename");
  for (const auto* entry : entries) {
    std::cout << std::format(
        "{:>10}  {:<7}  {}\n", pkgfile::db::PostingCountOf(*entry),
        pkgfile::db::HasInlinePosting(*entry) ? "yes" : "no",
        repo.ResolveString(entry->name));
  }
}

int CmdPostings(const MappedRepo& repo, std::string_view basename) {
  const BasenameEntry* entry = repo.FindBasename(basename);
  if (entry == nullptr) {
    std::cerr << std::format("error: no file named '{}' in this repo\n",
                             basename);
    return 1;
  }

  pkgfile::db::Posting scratch;
  for (const auto& posting : repo.PostingsFor(*entry, &scratch)) {
    const auto& pkg = repo.packages()[posting.pkg];
    std::cout << std::format(
        "{} {:<40} {}\n", pkgfile::db::IsDirOf(posting.path) ? "d" : "f",
        repo.ResolveString(pkg.name), repo.ResolvePath(posting.path));
  }
  return 0;
}

void CmdPaths(const MappedRepo& repo) {
  std::cout << std::format("{:>8}  {:>8}  {:<24} {}\n", "id", "parent", "name",
                           "resolved");
  for (pkgfile::db::PathId id = 0; id < repo.path_count(); ++id) {
    const PathNode& node = repo.PathNodeAt(id);
    const std::string parent = node.parent == pkgfile::db::kRootPath
                                   ? "-"
                                   : std::to_string(node.parent);
    std::cout << std::format("{:>8}  {:>8}  {:<24} {}\n", id, parent,
                             repo.ResolveString(node.name),
                             repo.ResolvePath(id));
  }
}

void CmdStrings(const MappedRepo& repo) {
  std::cout << std::format("{:>8}  {}\n", "id", "string");
  for (size_t id = 0; id < repo.string_count(); ++id) {
    std::cout << std::format("{:>8}  {}\n", id,
                             repo.ResolveString(static_cast<uint32_t>(id)));
  }
}

void Usage(const char* argv0) {
  std::cerr << std::format(R"(usage: {} <file.files> [command] [arg]

Debugging aid for inspecting a pkgfile repo database (PFDB). Not for
scripting against -- the format and this tool's output can both change
without notice.

Commands:
  (none)               header fields and a per-section size breakdown
  packages             list every package: name, version, file count
  files <pkgname>      list every file in <pkgname> (exact match)
  basenames            list every distinct basename, by posting count desc
  postings <basename>  list every (package, path) occurrence of <basename>
  paths                dump the raw path trie: id, parent, name, resolved
  strings              dump the raw string table: id -> string
)",
                           argv0);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    Usage(argv[0]);
    return 2;
  }

  const std::string path = argv[1];
  const std::string_view command = argc >= 3 ? argv[2] : "";
  const std::string_view arg = argc >= 4 ? argv[3] : "";

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path, &error);
  if (repo == nullptr) {
    std::cerr << std::format("error: failed to open '{}': {}\n", path,
                             OpenErrorToString(error));
    return 1;
  }

  if (command.empty()) {
    struct stat st{};
    stat(path.c_str(), &st);
    std::error_code ec;
    const uint64_t file_size = fs::file_size(path, ec);
    CmdSummary(*repo, ec ? 0 : file_size, st.st_mtime);
    return 0;
  }

  if (command == "packages") {
    CmdPackages(*repo);
    return 0;
  }

  if (command == "files") {
    if (arg.empty()) {
      std::cerr << "error: 'files' requires a package name\n";
      return 2;
    }
    return CmdFiles(*repo, arg);
  }

  if (command == "basenames") {
    CmdBasenames(*repo);
    return 0;
  }

  if (command == "postings") {
    if (arg.empty()) {
      std::cerr << "error: 'postings' requires a basename\n";
      return 2;
    }
    return CmdPostings(*repo, arg);
  }

  if (command == "paths") {
    CmdPaths(*repo);
    return 0;
  }

  if (command == "strings") {
    CmdStrings(*repo);
    return 0;
  }

  std::cerr << std::format("error: unknown command '{}'\n", command);
  Usage(argv[0]);
  return 2;
}

// vim: set ts=2 sw=2 et:
