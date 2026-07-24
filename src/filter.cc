#include "filter.hh"

#include <fnmatch.h>
#include <strings.h>

#include <format>
#include <iostream>

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
  if (!case_sensitive) {
    flags_ |= FNM_CASEFOLD;
  }
}

bool Glob::Matches(std::string_view line) const {
  // fnmatch needs a NUL-terminated string, but this runs on every candidate
  // line. Reuse a per-thread buffer to avoid an allocation per call. Matching
  // happens concurrently across worker threads, so the buffer must be
  // thread_local rather than a member.
  thread_local std::string buf;
  buf.assign(line);
  return fnmatch(glob_pattern_.c_str(), buf.c_str(), flags_) == 0;
}

Regex::~Regex() {
  pcre_free_study(re_extra_);
  pcre_free(re_);
}

// static
std::unique_ptr<Regex> Regex::Compile(const std::string& pattern,
                                      bool case_sensitive) {
  const int options = case_sensitive ? 0 : PCRE_CASELESS;
  const char* err;
  int offset;

  pcre* re = pcre_compile(pattern.c_str(), options, &err, &offset, nullptr);
  if (re == nullptr) {
    std::cerr << std::format("error: failed to compile regex at char {}: {}\n",
                             offset, err);
    return nullptr;
  }

  pcre_extra* re_extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &err);
  if (err) {
    std::cerr << std::format("error: failed to study regex: {}\n", err);
    pcre_free(re);
    return nullptr;
  }

  return std::make_unique<Regex>(re, re_extra);
}

bool Regex::Matches(std::string_view line) const {
  return pcre_exec(re_, re_extra_, line.data(), line.size(), 0,
                   PCRE_NO_UTF16_CHECK, nullptr, 0) >= 0;
}

Exact::Exact(std::string match, bool case_sensitive)
    : match_(std::move(match)), case_sensitive_(case_sensitive) {}

bool Exact::Matches(std::string_view line) const {
  if (case_sensitive_) {
    return line == match_;
  }

  return line.size() == match_.size() &&
         strncasecmp(line.data(), match_.data(), match_.size()) == 0;
}

}  // namespace filter
}  // namespace pkgfile
