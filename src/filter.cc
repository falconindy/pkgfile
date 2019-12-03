#include "filter.hh"

#include <fnmatch.h>
#include <string.h>

namespace pkgfile {
namespace filter {

bool Directory::Matches(std::string_view line) const {
  return !line.empty() && line.back() == '/';
}

bool Bin::Matches(std::string_view line) const {
  if (directory_filter_.Matches(line)) {
    return false;
  }

  return line.find("/bin/") != line.npos ||
         line.find("/sbin/") != line.npos;
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

Regex::~Regex() {
  pcre_free_study(re_extra_);
  pcre_free(re_);
}

// static
std::unique_ptr<Regex> Regex::Compile(const std::string& pattern,
                                      bool case_sensitive) {
  int options = case_sensitive ? 0 : PCRE_CASELESS;
  const char* err;
  int offset;

  pcre* re = pcre_compile(pattern.c_str(), options, &err, &offset, nullptr);
  if (re == nullptr) {
    fprintf(stderr, "error: failed to compile regex at char %d: %s\n", offset,
            err);
    return nullptr;
  }

  pcre_extra* re_extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &err);
  if (err) {
    fprintf(stderr, "error: failed to study regex: %s\n", err);
    pcre_free(re);
    return nullptr;
  }

  return std::make_unique<Regex>(re, re_extra);
}

bool Regex::Matches(std::string_view line) const {
  return pcre_exec(re_, re_extra_, line.data(), line.size(), 0,
                   PCRE_NO_UTF16_CHECK, nullptr, 0) >= 0;
}

Exact::Exact(std::string match, bool case_sensitive) {
  if (case_sensitive) {
    predicate_ = [m = std::move(match)](std::string_view line) {
      return m == line;
    };
  } else {
    predicate_ = [m = std::move(match)](std::string_view line) {
      if (line.size() != m.size()) {
        return false;
      }

      return strncasecmp(line.data(), m.data(), m.size()) == 0;
    };
  }
}

bool Exact::Matches(std::string_view line) const { return predicate_(line); }

Basename::Basename(std::string match, bool case_sensitive)
    : predicate_(std::make_unique<Exact>(match, case_sensitive)) {}

bool Basename::Matches(std::string_view line) const {
  auto pos = line.rfind('/');
  if (pos != line.npos) {
    line.remove_prefix(pos + 1);
  }

  return predicate_->Matches(line);
}

}  // namespace filter
}  // namespace pkgfile
