#include "result.hh"

#include <algorithm>
#include <mutex>

namespace pkgfile {

void Result::PrintTwoColumns(size_t prefixlen, char eol) const {
  for (const auto& line : lines_) {
    printf("%-*s\t%s%c", static_cast<int>(prefixlen), line.prefix.c_str(),
           line.entry.c_str(), eol);
  }
}

void Result::PrintOneColumn(char eol) const {
  for (const auto& line : lines_) {
    printf("%s%c", line.prefix.c_str(), eol);
  }
}

void Result::Add(std::string prefix, std::string entry) {
  std::lock_guard l(mu_);

  if (prefix.size() > max_prefixlen_) {
    max_prefixlen_ = prefix.size();
  }

  lines_.emplace_back(std::move(prefix), std::move(entry));
}

void Result::MergeFrom(Batch* batch) {
  if (batch->lines_.empty()) {
    return;
  }

  std::lock_guard l(mu_);

  if (batch->max_prefixlen_ > max_prefixlen_) {
    max_prefixlen_ = batch->max_prefixlen_;
  }

  lines_.insert(lines_.end(), std::make_move_iterator(batch->lines_.begin()),
                std::make_move_iterator(batch->lines_.end()));

  batch->lines_.clear();
  batch->max_prefixlen_ = 0;
}

void Result::Print(size_t prefixlen, char eol) {
  if (lines_.empty()) {
    return;
  }

  std::sort(lines_.begin(), lines_.end(),
            [](const struct Line& a, const struct Line& b) {
              if (a.prefix == b.prefix && !b.prefix.empty()) {
                return a.entry < b.entry;
              }

              return a.prefix < b.prefix;
            });

  // It's expected that results are homogenous, so we can trust the first line.
  if (!lines_[0].entry.empty()) {
    PrintTwoColumns(prefixlen, eol);
  } else {
    PrintOneColumn(eol);
  }
}

size_t MaxPrefixlen(const std::vector<Result>& results) {
  return std::max_element(results.begin(), results.end(),
                          [](const Result& a, const Result& b) {
                            return a.MaxPrefixlen() < b.MaxPrefixlen();
                          })
      ->MaxPrefixlen();
}

}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
