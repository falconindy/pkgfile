#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "db_format.hh"

namespace pkgfile::db {

// Builds a PFDB repo database in memory from a repo's upstream `.files`
// archive, then serializes it to disk.
//
// Usage:
//   auto builder = DbBuilder::FromArchive(reponame, fd, &error);
//   if (builder == nullptr) { ... }
//   builder->WriteToFile(path, mtime);
class DbBuilder {
 public:
  explicit DbBuilder(std::string reponame);

  DbBuilder(const DbBuilder&) = delete;
  DbBuilder& operator=(const DbBuilder&) = delete;

  // Reads every package's file list out of the upstream ALPM `.files`
  // archive on `fd_in` (a tarball of `$pkgname-$pkgver-$pkgrel/files` member
  // files) and interns it into a new builder. Returns nullptr and sets
  // *error on failure.
  //
  // If given, `on_progress` is called after each package with the
  // cumulative number of (compressed) bytes consumed from `fd_in` so far --
  // compare against the archive's known total size for a repack progress
  // indicator.
  static std::unique_ptr<DbBuilder> FromArchive(
      std::string reponame, int fd_in, const char** error,
      const std::function<void(int64_t bytes_read)>& on_progress = nullptr);

  // Interns one package's file list. `files` pairs a path (no leading slash,
  // no trailing slash even for directories) with whether it's a directory.
  void AddPackage(std::string_view name, std::string_view version,
                  const std::vector<std::pair<std::string, bool>>& files);

  // Serializes the accumulated database and atomically replaces `path` with
  // it (writes to `path + "~"` then renames over `path`). `mtime` is the
  // mtime of the upstream archive this was built from; it's stamped onto the
  // resulting file (both atime and mtime) so a later stat() of `path` can be
  // used to decide whether a repo needs re-downloading. Returns false on
  // failure, leaving `path` untouched.
  bool WriteToFile(const std::string& path, int64_t mtime);

 private:
  StringId InternString(std::string_view s);
  PathId InternPath(std::string_view path);

  std::string reponame_;

  // Bump-allocated storage for every interned string's bytes. Unlike a
  // deque<string>/vector<string>, this costs no per-string heap allocation
  // and no per-string object overhead (sizeof(std::string) is 32 bytes in
  // libstdc++, more than most path components) -- just the raw bytes,
  // packed into large chunks. A string_view returned by Add() stays valid
  // for the arena's lifetime: chunks, once allocated, never move or get
  // reused.
  class StringArena {
   public:
    std::string_view Add(std::string_view s) {
      if (s.size() > kChunkSize) {
        // Rare (longer than a whole chunk): give it its own allocation
        // rather than complicating the common bump-allocation path below.
        auto chunk = std::make_unique<char[]>(s.size());
        memcpy(chunk.get(), s.data(), s.size());
        const std::string_view view(chunk.get(), s.size());
        chunks_.push_back(std::move(chunk));
        return view;
      }

      if (used_ + s.size() > current_size_) {
        chunks_.push_back(std::make_unique<char[]>(kChunkSize));
        current_ = chunks_.back().get();
        current_size_ = kChunkSize;
        used_ = 0;
      }

      char* dst = current_ + used_;
      memcpy(dst, s.data(), s.size());
      used_ += s.size();
      return std::string_view(dst, s.size());
    }

   private:
    static constexpr size_t kChunkSize = 1 << 20;  // 1 MiB

    std::vector<std::unique_ptr<char[]>> chunks_;
    char* current_ = nullptr;
    size_t current_size_ = 0;
    size_t used_ = 0;
  };
  StringArena string_arena_;

  // Order-preserving StringId -> content: element i is the string that
  // InternString() assigned id i, as a view into string_arena_.
  std::vector<std::string_view> strings_;

  // Open-addressing map from string content to StringId, used only while
  // interning strings in InternString(). Same rationale as PathIndex below:
  // real repos intern millions of strings, so a flat array beats
  // std::unordered_map's per-entry heap node at that scale.
  class StringIndex {
   public:
    // Returns the StringId already stored for `key`, calling `make_id()` to
    // intern it (copying it into stable storage) and assign a new one if
    // `key` hasn't been seen before. `make_id` must return the new
    // (StringId, interned view) pair and must not touch this StringIndex
    // (no reentrant calls).
    template <typename MakeId>
    StringId GetOrInsert(std::string_view key, MakeId make_id) {
      if (slots_.empty()) {
        Grow(kInitialCapacity);
      }

      size_t i = Probe(key);
      if (slots_[i].occupied) {
        return slots_[i].value;
      }

      const auto [id, interned] = make_id();
      if (++size_ > slots_.size() * 7 / 10) {
        Grow(slots_.size() + slots_.size() / 2);
        i = Probe(interned);
      }
      slots_[i] = Slot{interned, id, true};
      return id;
    }

   private:
    struct Slot {
      std::string_view key;
      StringId value = 0;
      bool occupied = false;
    };
    static constexpr size_t kInitialCapacity = 16;

    // Capacity isn't a power of 2 (see PathIndex below for why), so indexing
    // uses `%` rather than a bitmask.
    size_t Probe(std::string_view key) const {
      const size_t cap = slots_.size();
      size_t i = std::hash<std::string_view>{}(key) % cap;
      while (slots_[i].occupied && slots_[i].key != key) {
        i = (i + 1) % cap;
      }
      return i;
    }

    void Grow(size_t new_capacity) {
      std::vector<Slot> old = std::move(slots_);
      slots_.assign(new_capacity, Slot{});
      for (const Slot& s : old) {
        if (!s.occupied) {
          continue;
        }
        size_t i = std::hash<std::string_view>{}(s.key) % new_capacity;
        while (slots_[i].occupied) {
          i = (i + 1) % new_capacity;
        }
        slots_[i] = s;
      }
    }

    std::vector<Slot> slots_;
    size_t size_ = 0;
  };
  StringIndex string_lookup_;

  std::vector<PathNode> paths_;

  // Open-addressing map from (parent PathId << 32 | name StringId) to
  // PathId, used only while interning paths in InternPath(). Real repos
  // intern several million distinct paths -- more entries than any other
  // build-time table here, including string_lookup_ above -- so unlike the
  // std::unordered_maps elsewhere in this class, this one is a flat array:
  // one contiguous allocation with no per-entry heap node, which is the
  // dominant saving at that scale.
  class PathIndex {
   public:
    // Returns the PathId already stored for `key`, calling `make_id()` to
    // create and insert one if `key` hasn't been seen before. `make_id` must
    // not touch this PathIndex (no reentrant calls).
    template <typename MakeId>
    PathId GetOrInsert(uint64_t key, MakeId make_id) {
      if (slots_.empty()) {
        Grow(kInitialCapacity);
      }

      size_t i = Probe(key);
      if (slots_[i].occupied) {
        return slots_[i].value;
      }

      const PathId id = make_id();
      if (++size_ > slots_.size() * 7 / 10) {
        // 1.5x rather than the usual doubling: for the several-million-entry
        // tables a real repo produces, doubling both wastes more steady-state
        // space (final load factor can be as low as ~35%) and, worse, means
        // the last grow step transiently holds a full old *and* new array at
        // once (old + 2x old = 3x old just for that step). 1.5x tracks
        // actual occupancy more tightly and caps that transient at 2.5x old.
        Grow(slots_.size() + slots_.size() / 2);
        i = Probe(key);
      }
      slots_[i] = Slot{key, id, true};
      return id;
    }

   private:
    struct Slot {
      uint64_t key = 0;
      PathId value = 0;
      bool occupied = false;
    };
    static constexpr size_t kInitialCapacity = 16;

    // Capacity isn't a power of 2 (see the 1.5x growth factor above), so
    // indexing uses `%` rather than a bitmask.
    size_t Probe(uint64_t key) const {
      const size_t cap = slots_.size();
      size_t i = Hash(key) % cap;
      while (slots_[i].occupied && slots_[i].key != key) {
        i = (i + 1) % cap;
      }
      return i;
    }

    void Grow(size_t new_capacity) {
      std::vector<Slot> old = std::move(slots_);
      slots_.assign(new_capacity, Slot{});
      for (const Slot& s : old) {
        if (!s.occupied) {
          continue;
        }
        size_t i = Hash(s.key) % new_capacity;
        while (slots_[i].occupied) {
          i = (i + 1) % new_capacity;
        }
        slots_[i] = s;
      }
    }

    // splitmix64's finalizer: fast, with good avalanche for the sequential,
    // structured keys InternPath() generates.
    static size_t Hash(uint64_t key) {
      key ^= key >> 33;
      key *= 0xff51afd7ed558ccdULL;
      key ^= key >> 33;
      key *= 0xc4ceb9fe1a85ec53ULL;
      key ^= key >> 33;
      return static_cast<size_t>(key);
    }

    std::vector<Slot> slots_;
    size_t size_ = 0;
  };
  PathIndex path_lookup_;

  struct PendingPackage {
    StringId name;
    StringId version;
    std::vector<uint32_t> tagged_files;  // PathId, tagged with kDirBit
  };
  std::vector<PendingPackage> packages_;

  // Every (package, path) occurrence of every basename, threaded into a
  // singly linked list per basename instead of a separate vector<Posting>
  // per basename: real repos have millions of basenames with exactly one
  // occurrence each (see db_format.hh), and a `vector` per key means a
  // separate heap allocation for each of them. Packages are referenced by
  // their index into `packages_` until WriteToFile() remaps them to their
  // final, name-sorted PkgId.
  struct PendingPosting {
    Posting posting;
    uint32_t prev;  // index into pending_postings_, or kNoPosting
  };
  static constexpr uint32_t kNoPosting = 0xFFFFFFFFu;
  std::vector<PendingPosting> pending_postings_;
  // Basename StringId -> index into pending_postings_ of its most recently
  // added occurrence (the head of that basename's list).
  std::unordered_map<StringId, uint32_t> basename_heads_;
};

}  // namespace pkgfile::db

// vim: set ts=2 sw=2 et:
