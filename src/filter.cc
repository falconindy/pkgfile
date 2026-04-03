#include "filter.hh"

#include <fnmatch.h>
#include <string.h>

#include <format>
#include <iostream>
#include <re2/re2.h>

namespace pkgfile {
namespace filter {

bool Directory::Matches(std::string_view line) const {
  return line.ends_with('/');
}

bool Bin::Matches(std::string_view line) const {
  if (directory_filter_.Matches(line)) {
    return false;
  }

  for (const auto& bin : bins_) {
    // Binaries must start with a PATH component and must be in a subdir of the
    // component.
    if (line.starts_with(bin)) {
      return true;
    }
  }

  return false;
}

Glob::Glob(std::string glob_pattern, bool case_sensitive)
    : glob_pattern_(std::move(glob_pattern)), flags_(FNM_PATHNAME) {
  if (case_sensitive) {
    flags_ |= FNM_CASEFOLD;
  }
}

bool Glob::Matches(std::string_view line) const {
  return fnmatch(glob_pattern_.c_str(), std::string(line).c_str(), flags_) == 0;
}

// static
std::unique_ptr<Regex> Regex::Compile(const std::string& pattern,
                                      bool case_sensitive) {
  re2::RE2::Options re2_options;
  re2_options.set_case_sensitive(case_sensitive);
  re2_options.set_log_errors(false);

  auto re = std::make_unique<re2::RE2>(pattern, re2_options);
  if (!re->ok()) {
    std::cerr << std::format("error: failed to compile regex: {}\n",
                             re->error());
    return nullptr;
  }

  return std::make_unique<Regex>(std::move(re));
}

bool Regex::Matches(std::string_view line) const {
  return re2::RE2::PartialMatch(line, *re_);
}

Exact::Exact(std::string match, bool case_sensitive) {
  if (case_sensitive) {
    predicate_ = [m = std::move(match)](std::string_view line) {
      return m == line;
    };
  } else {
    predicate_ = [m = std::move(match)](std::string_view line) {
      return line.size() == m.size() &&
             strncasecmp(line.data(), m.data(), m.size()) == 0;
    };
  }
}

bool Exact::Matches(std::string_view line) const { return predicate_(line); }

Basename::Basename(std::string match, bool case_sensitive)
    : predicate_(std::make_unique<Exact>(match, case_sensitive)) {}

bool Basename::Matches(std::string_view line) const {
  const void* p = memrchr(line.data(), '/', line.size());
  if (p == nullptr) {
    return predicate_->Matches(line);
  }

  const std::string_view base(static_cast<const char*>(p) + 1, line.end());
  return predicate_->Matches(base);
}

}  // namespace filter
}  // namespace pkgfile
