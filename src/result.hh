#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace pkgfile {

class Result {
 public:
  Result() = default;

  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  bool Empty() const { return lines_.empty(); }

  void Add(std::string prefix, std::string entry);
  void Print(size_t prefixlen, char eol);
  size_t MaxPrefixlen() const { return max_prefixlen_; }

 private:
  struct Line {
    Line(std::string prefix, std::string entry)
        : prefix(std::move(prefix)), entry(std::move(entry)) {}

    std::string prefix;
    std::string entry;
  };

  std::mutex mu_;
  std::vector<Line> lines_;
  size_t max_prefixlen_ = 0;

  void PrintOneColumn(char eol) const;
  void PrintTwoColumns(size_t prefixlen, char eol) const;
};

size_t MaxPrefixlen(const std::vector<Result>& results);

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
