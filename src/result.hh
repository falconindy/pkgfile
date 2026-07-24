#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace pkgfile {

class Result {
 private:
  struct Line {
    Line(std::string prefix, std::string entry)
        : prefix(std::move(prefix)), entry(std::move(entry)) {}

    std::string prefix;
    std::string entry;
  };

 public:
  Result() = default;

  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  bool Empty() const { return lines_.empty(); }

  void Add(std::string prefix, std::string entry);
  void Print(size_t prefixlen, char eol);
  size_t MaxPrefixlen() const { return max_prefixlen_; }

  // An unsynchronized accumulator for a single thread's matches: fill it
  // via Add() with no locking, then hand the whole batch to the owning
  // Result's MergeFrom() once (e.g. once per work chunk, rather than once
  // per match) to fold it in under a single lock acquisition. Left empty
  // after a merge, so it's safe to reuse across multiple MergeFrom() calls.
  class Batch {
   public:
    void Add(std::string prefix, std::string entry) {
      if (prefix.size() > max_prefixlen_) {
        max_prefixlen_ = prefix.size();
      }
      lines_.emplace_back(std::move(prefix), std::move(entry));
    }

   private:
    friend class Result;

    std::vector<Line> lines_;
    size_t max_prefixlen_ = 0;
  };

  // Folds `batch` into this Result under one lock acquisition and leaves
  // `batch` empty. A no-op if `batch` is empty, so callers can merge
  // unconditionally without checking first.
  void MergeFrom(Batch* batch);

 private:
  std::mutex mu_;
  std::vector<Line> lines_;
  size_t max_prefixlen_ = 0;

  void PrintOneColumn(char eol) const;
  void PrintTwoColumns(size_t prefixlen, char eol) const;
};

size_t MaxPrefixlen(const std::vector<Result>& results);

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
