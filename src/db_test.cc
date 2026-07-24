#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "db_builder.hh"
#include "db_format.hh"
#include "gtest/gtest.h"
#include "mapped_repo.hh"

namespace fs = std::filesystem;

namespace pkgfile::db {
namespace {

class TempPfdbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = (fs::temp_directory_path() /
             std::format("pkgfile_db_test_{}.files", getpid()))
                .string();
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove(path_, ec);
  }

  std::string path_;
};

using DbRoundTripTest = TempPfdbTest;

TEST_F(DbRoundTripTest, RoundTripsPackagesAndFiles) {
  DbBuilder builder("testrepo");

  builder.AddPackage("bash", "5.1-1",
                     {
                         {"usr", true},
                         {"usr/bin", true},
                         {"usr/bin/bash", false},
                         {"usr/share", true},
                         {"usr/share/licenses", true},
                         {"usr/share/licenses/bash", true},
                         {"usr/share/licenses/bash/LICENSE", false},
                     });
  builder.AddPackage("attr", "2.5.1-1",
                     {
                         {"usr", true},
                         {"usr/bin", true},
                         {"usr/bin/getfattr", false},
                     });

  ASSERT_TRUE(builder.WriteToFile(path_, 1234567890));

  struct stat st;
  ASSERT_EQ(stat(path_.c_str(), &st), 0);
  EXPECT_EQ(st.st_mtime, 1234567890);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  EXPECT_EQ(repo->reponame(), "testrepo");

  // Packages are sorted by name, so "attr" precedes "bash".
  ASSERT_EQ(repo->packages().size(), 2u);
  EXPECT_EQ(repo->ResolveString(repo->packages()[0].name), "attr");
  EXPECT_EQ(repo->ResolveString(repo->packages()[1].name), "bash");

  const Package* bash = repo->FindPackageByName("bash");
  ASSERT_NE(bash, nullptr);
  EXPECT_EQ(repo->ResolveString(bash->version), "5.1-1");

  std::set<std::string> bash_files;
  for (const auto tagged : repo->PackageFiles(*bash)) {
    bash_files.insert(repo->ResolvePath(tagged));
  }
  EXPECT_EQ(bash_files,
            (std::set<std::string>{"/usr/", "/usr/bin/", "/usr/bin/bash",
                                   "/usr/share/", "/usr/share/licenses/",
                                   "/usr/share/licenses/bash/",
                                   "/usr/share/licenses/bash/LICENSE"}));

  EXPECT_EQ(repo->FindPackageByName("doesnotexist"), nullptr);

  // "bash" the package ships two files literally named "bash": the binary
  // and a directory (usr/share/licenses/bash/), so this basename has two
  // occurrences and must NOT be inlined -- it should round-trip through the
  // postings pool instead.
  const auto* basename_entry = repo->FindBasename("bash");
  ASSERT_NE(basename_entry, nullptr);
  EXPECT_FALSE(HasInlinePosting(*basename_entry));
  Posting bash_scratch;
  const auto bash_postings = repo->PostingsFor(*basename_entry, &bash_scratch);
  ASSERT_EQ(bash_postings.size(), 2u);
  bool found_binary = false;
  for (const auto& posting : bash_postings) {
    EXPECT_EQ(repo->ResolveString(repo->packages()[posting.pkg].name), "bash");
    if (repo->ResolvePath(posting.path) == "/usr/bin/bash") {
      found_binary = true;
      EXPECT_FALSE(IsDirOf(posting.path));
    }
  }
  EXPECT_TRUE(found_binary);

  // Only "attr" ships a file named "getfattr", and only once: this is the
  // common case the inline encoding exists for.
  const auto* getfattr_entry = repo->FindBasename("getfattr");
  ASSERT_NE(getfattr_entry, nullptr);
  EXPECT_TRUE(HasInlinePosting(*getfattr_entry));
  Posting getfattr_scratch;
  const auto getfattr_postings =
      repo->PostingsFor(*getfattr_entry, &getfattr_scratch);
  ASSERT_EQ(getfattr_postings.size(), 1u);
  EXPECT_EQ(
      repo->ResolveString(repo->packages()[getfattr_postings[0].pkg].name),
      "attr");

  // "usr" is a directory shared by both packages, so it also has two
  // occurrences and must NOT be inlined.
  const auto* usr_entry = repo->FindBasename("usr");
  ASSERT_NE(usr_entry, nullptr);
  EXPECT_FALSE(HasInlinePosting(*usr_entry));
  Posting usr_scratch;
  const auto usr_postings = repo->PostingsFor(*usr_entry, &usr_scratch);
  ASSERT_EQ(usr_postings.size(), 2u);
  for (const auto& posting : usr_postings) {
    EXPECT_TRUE(IsDirOf(posting.path));
    EXPECT_EQ(repo->ResolvePath(posting.path), "/usr/");
  }

  EXPECT_EQ(repo->FindBasename("doesnotexist"), nullptr);
}

TEST_F(DbRoundTripTest, InternsSharedDirectoryPrefixesOnce) {
  DbBuilder builder("testrepo");

  // Two packages sharing the entire "usr/bin/" prefix: the underlying path
  // table should only ever grow by the components unique to each package
  // (one leaf node each), not duplicate "usr" and "usr/bin" a second time.
  builder.AddPackage("pkg-a", "1-1",
                     {{"usr", true}, {"usr/bin", true}, {"usr/bin/a", false}});
  builder.AddPackage("pkg-b", "1-1",
                     {{"usr", true}, {"usr/bin", true}, {"usr/bin/b", false}});

  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  // 4 distinct paths total: usr/, usr/bin/, usr/bin/a, usr/bin/b. Without
  // interning this would be 6 (3 components x 2 packages).
  EXPECT_EQ(repo->path_count(), 4u);
}

TEST_F(DbRoundTripTest, ResolvesPathsDeeperThanInlineStackBuffer) {
  DbBuilder builder("testrepo");

  // Deeper than any real package file list gets close to, deliberately
  // exceeding ResolvePathInto's fixed-size stack buffer so its fallback path
  // (a second trie walk instead of the stack array) gets exercised too.
  constexpr int kDepth = 200;
  std::string path;
  std::vector<std::pair<std::string, bool>> files;
  for (int i = 0; i < kDepth; ++i) {
    if (i > 0) {
      path += "/";
    }
    path += std::format("d{}", i);
    files.emplace_back(path, true);
  }
  files.emplace_back(path + "/leaf", false);

  builder.AddPackage("deep", "1-1", files);
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  const Package* pkg = repo->FindPackageByName("deep");
  ASSERT_NE(pkg, nullptr);

  const std::string expected_dir = "/" + path + "/";
  const std::string expected_leaf = "/" + path + "/leaf";

  std::set<std::string> resolved;
  for (const auto tagged : repo->PackageFiles(*pkg)) {
    resolved.insert(repo->ResolvePath(tagged));
  }

  EXPECT_EQ(resolved.count(expected_dir), 1u);
  EXPECT_EQ(resolved.count(expected_leaf), 1u);
  EXPECT_EQ(resolved.size(), static_cast<size_t>(kDepth + 1));
}

TEST_F(DbRoundTripTest, PathCacheMatchesUncachedResolution) {
  DbBuilder builder("testrepo");

  // A mix designed to exercise every PathCache transition: runs of files
  // sharing an immediate parent (cache hits), moves to a sibling directory
  // (parent changes but shares an ancestor), a jump back up to a shallower
  // directory, and -- since the trie is deduplicated repo-wide -- a second
  // package landing back in a directory the first package already visited.
  builder.AddPackage(
      "pkg-a", "1-1",
      {
          {"usr", true},
          {"usr/bin", true},
          {"usr/bin/a1", false},
          {"usr/bin/a2", false},
          {"usr/bin/a3", false},
          {"usr/share", true},
          {"usr/share/doc", true},
          {"usr/share/doc/pkg-a", true},
          {"usr/share/doc/pkg-a/README", false},
          {"usr/share/doc/pkg-a/CHANGELOG", false},
          {"usr/bin", true},  // back up to a shallower, already-seen dir
      });
  builder.AddPackage("pkg-b", "1-1",
                     {
                         {"usr", true},
                         {"usr/bin", true},
                         {"usr/bin/b1", false},  // same parent as pkg-a's files
                     });

  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  MappedRepo::PathCache cache;
  std::string cached;

  for (const auto* name : {"pkg-a", "pkg-b"}) {
    const Package* pkg = repo->FindPackageByName(name);
    ASSERT_NE(pkg, nullptr);

    for (const auto tagged : repo->PackageFiles(*pkg)) {
      const std::string uncached = repo->ResolvePath(tagged);
      repo->ResolvePathInto(tagged, &cached, &cache);
      EXPECT_EQ(cached, uncached)
          << "package " << name << " tagged path " << tagged;
    }
  }
}

// Below: MappedRepo is a zero-copy view straight over a PFDB file's bytes,
// so nothing about a corrupt or maliciously crafted file stops it from
// containing an in-range table full of out-of-range interior references (a
// StringId, PathId, or PkgId that doesn't fit the table it indexes into).
// These tests build a valid db with DbBuilder, then poke a single such
// reference bad by hand and confirm the accessor that would dereference it
// degrades safely -- an empty string/span or a terminal sentinel -- instead
// of indexing outside the mapping. Open() itself is expected to keep
// succeeding throughout: it only validates that whole tables fit in the
// file, not what's inside them, which is what keeps it cheap on every real
// (uncorrupted) db.

std::string ReadWholeFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}

void WriteWholeFile(const std::string& path, std::string_view data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

// Overwrites the bytes of `bytes` at `offset` with `value`'s raw
// representation -- pokes a single corrupt field into an otherwise-valid
// PFDB file's byte buffer. Every struct in db_format.hh is #pragma pack(1),
// so this lines up exactly with the on-disk layout.
template <typename T>
void PokeAt(std::string* bytes, uint64_t offset, const T& value) {
  memcpy(bytes->data() + offset, &value, sizeof(T));
}

using MappedRepoCorruptionTest = TempPfdbTest;

TEST_F(MappedRepoCorruptionTest, ResolveStringRejectsOutOfRangeByteRange) {
  DbBuilder builder("testrepo");
  builder.AddPackage("pkg", "1-1", {{"usr/bin/foo", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  std::string bytes = ReadWholeFile(path_);
  Header header;
  memcpy(&header, bytes.data(), sizeof(header));

  // Claim a byte range that reaches far past the (tiny) byte pool this repo
  // actually has.
  PokeAt(&bytes, header.string_table_offset, StringRef{0, 0xFFFFFFFFu});
  WriteWholeFile(path_, bytes);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  EXPECT_EQ(repo->ResolveString(0), "");
}

TEST_F(MappedRepoCorruptionTest, ResolveStringRejectsIdPastTableEnd) {
  DbBuilder builder("testrepo");
  builder.AddPackage("pkg", "1-1", {{"usr/bin/foo", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  EXPECT_EQ(
      repo->ResolveString(static_cast<StringId>(repo->string_count() + 100)),
      "");
}

TEST_F(MappedRepoCorruptionTest, PathNodeAtRejectsForwardParent) {
  DbBuilder builder("testrepo");
  builder.AddPackage(
      "pkg", "1-1", {{"usr", true}, {"usr/bin", true}, {"usr/bin/foo", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  std::string bytes = ReadWholeFile(path_);
  Header header;
  memcpy(&header, bytes.data(), sizeof(header));

  // Node 0 ("usr", the first path interned) must have kRootPath as its
  // parent in any legitimately-built db, since the builder always appends a
  // parent before its children. Point it at a later node instead -- the
  // shape of reference that would otherwise send a trie walk into an
  // infinite loop on a corrupt file.
  PathNode original;
  memcpy(&original, bytes.data() + header.path_table_offset, sizeof(original));
  PokeAt(&bytes, header.path_table_offset,
         PathNode{/*parent=*/2, original.name});
  WriteWholeFile(path_, bytes);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  EXPECT_EQ(repo->PathNodeAt(0).parent, kRootPath);
}

TEST_F(MappedRepoCorruptionTest, PathNodeAtRejectsIdPastTableEnd) {
  DbBuilder builder("testrepo");
  builder.AddPackage("pkg", "1-1", {{"usr/bin/foo", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  const PathId bad_id = static_cast<PathId>(repo->path_count() + 100);
  EXPECT_EQ(repo->PathNodeAt(bad_id).parent, kRootPath);
}

TEST_F(MappedRepoCorruptionTest, PackageFilesClampsCorruptSlice) {
  DbBuilder builder("testrepo");
  builder.AddPackage("pkg", "1-1", {{"usr/bin/foo", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  size_t pkg_index;
  Package original;
  {
    MappedRepo::OpenError error;
    auto repo = MappedRepo::Open(path_, &error);
    ASSERT_NE(repo, nullptr);
    const Package* pkg = repo->FindPackageByName("pkg");
    ASSERT_NE(pkg, nullptr);
    pkg_index = pkg - repo->packages().data();
    original = *pkg;
  }

  std::string bytes = ReadWholeFile(path_);
  Header header;
  memcpy(&header, bytes.data(), sizeof(header));
  const size_t pkg_offset =
      header.package_table_offset + pkg_index * sizeof(Package);

  // Claim far more files than the package files pool actually holds.
  PokeAt(&bytes, pkg_offset,
         Package{original.name, original.version, /*files_start=*/0,
                 /*files_count=*/1'000'000});
  WriteWholeFile(path_, bytes);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);
  const Package* pkg = repo->FindPackageByName("pkg");
  ASSERT_NE(pkg, nullptr);

  const auto files = repo->PackageFiles(*pkg);
  EXPECT_LE(files.size(), header.package_files_count);
  for (const auto tagged : files) {
    repo->ResolvePath(tagged);  // must not read out of bounds
  }

  // A files_start beyond the pool entirely must clamp to an empty span
  // rather than being treated as in-bounds.
  bytes = ReadWholeFile(path_);
  PokeAt(&bytes, pkg_offset,
         Package{original.name, original.version, /*files_start=*/1'000'000,
                 /*files_count=*/1});
  WriteWholeFile(path_, bytes);

  repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);
  pkg = repo->FindPackageByName("pkg");
  ASSERT_NE(pkg, nullptr);
  EXPECT_TRUE(repo->PackageFiles(*pkg).empty());
}

TEST_F(MappedRepoCorruptionTest, PostingsForRejectsCorruptPooledEntry) {
  DbBuilder builder("testrepo");
  // Same basename ("bash") occurring in two different packages, so it lands
  // in the shared postings pool rather than being inlined.
  builder.AddPackage("bash", "5.1-1", {{"usr/bin/bash", false}});
  builder.AddPackage("attr", "2.5.1-1", {{"usr/local/bin/bash", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  size_t entry_index;
  uint32_t postings_start;
  {
    MappedRepo::OpenError error;
    auto repo = MappedRepo::Open(path_, &error);
    ASSERT_NE(repo, nullptr);
    const BasenameEntry* entry = repo->FindBasename("bash");
    ASSERT_NE(entry, nullptr);
    ASSERT_FALSE(HasInlinePosting(*entry));
    entry_index = entry - repo->basename_index().data();
    postings_start = entry->postings_start;
  }

  std::string bytes = ReadWholeFile(path_);
  Header header;
  memcpy(&header, bytes.data(), sizeof(header));

  // Corrupt the pkg field of this basename's first pooled Posting so it
  // points past the package table.
  const size_t posting_offset =
      header.postings_offset + postings_start * sizeof(Posting);
  Posting original;
  memcpy(&original, bytes.data() + posting_offset, sizeof(original));
  PokeAt(&bytes, posting_offset, Posting{/*pkg=*/0xFFFFFFFFu, original.path});
  WriteWholeFile(path_, bytes);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  const BasenameEntry* entry = repo->basename_index().data() + entry_index;
  Posting scratch;
  EXPECT_TRUE(repo->PostingsFor(*entry, &scratch).empty());
}

TEST_F(MappedRepoCorruptionTest, PostingsForRejectsCorruptInlinePkg) {
  DbBuilder builder("testrepo");
  builder.AddPackage("attr", "2.5.1-1", {{"usr/bin/getfattr", false}});
  ASSERT_TRUE(builder.WriteToFile(path_, 0));

  size_t entry_index;
  {
    MappedRepo::OpenError error;
    auto repo = MappedRepo::Open(path_, &error);
    ASSERT_NE(repo, nullptr);
    const BasenameEntry* entry = repo->FindBasename("getfattr");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(HasInlinePosting(*entry));
    entry_index = entry - repo->basename_index().data();
  }

  std::string bytes = ReadWholeFile(path_);
  Header header;
  memcpy(&header, bytes.data(), sizeof(header));
  const size_t entry_offset =
      header.basename_index_offset + entry_index * sizeof(BasenameEntry);

  BasenameEntry original;
  memcpy(&original, bytes.data() + entry_offset, sizeof(original));

  // Keep the inline-posting bit set, but corrupt the inlined PkgId to the
  // largest value the mask can hold -- well past this repo's one-entry
  // package table.
  PokeAt(&bytes, entry_offset,
         BasenameEntry{original.name, kInlinePostingBit | kPostingsStartMask,
                       original.postings_count});
  WriteWholeFile(path_, bytes);

  MappedRepo::OpenError error;
  auto repo = MappedRepo::Open(path_, &error);
  ASSERT_NE(repo, nullptr);

  const BasenameEntry* entry = repo->basename_index().data() + entry_index;
  ASSERT_TRUE(HasInlinePosting(*entry));
  Posting scratch;
  EXPECT_TRUE(repo->PostingsFor(*entry, &scratch).empty());
}

}  // namespace
}  // namespace pkgfile::db
