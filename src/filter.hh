#pragma once

#include <pcre.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pkgfile {
namespace filter {

class Filter {
 public:
  virtual ~Filter() {}
  virtual bool Matches(std::string_view line) const = 0;
};

class Not : public Filter {
 public:
  explicit Not(std::unique_ptr<Filter> inner_filter)
      : inner_filter_(std::move(inner_filter)) {}

  bool Matches(std::string_view line) const override {
    return !inner_filter_->Matches(line);
  }

 private:
  std::unique_ptr<Filter> inner_filter_;
};

class And : public Filter {
 public:
  And(std::unique_ptr<Filter> lhs, std::unique_ptr<Filter> rhs)
      : lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

  bool Matches(std::string_view line) const override {
    return lhs_->Matches(line) && rhs_->Matches(line);
  }

 private:
  std::unique_ptr<Filter> lhs_;
  std::unique_ptr<Filter> rhs_;
};

class Directory : public Filter {
 public:
  Directory() {}

  bool Matches(std::string_view line) const override;
};

class Bin : public Filter {
 public:
  explicit Bin(const std::vector<std::string>& bins) : bins_(bins) {}

  bool Matches(std::string_view line) const override;

 private:
  // We use this as an optimization for throwing out things early, i.e.
  // directories can't be binaries.
  Directory directory_filter_;
  const std::vector<std::string>& bins_;
};

class Regex : public Filter {
 public:
  Regex(pcre* re, pcre_extra* re_extra) : re_(re), re_extra_(re_extra) {}
  ~Regex();

  static std::unique_ptr<Regex> Compile(const std::string& pattern,
                                        bool case_sensitive);

  bool Matches(std::string_view line) const override;

 private:
  pcre* re_;
  pcre_extra* re_extra_;
};

class Glob : public Filter {
 public:
  Glob(std::string glob_pattern, bool case_sensitive);

  bool Matches(std::string_view line) const override;

 private:
  std::string glob_pattern_;
  int flags_;
};

class Exact : public Filter {
 public:
  Exact(std::string match, bool case_sensitive);

  bool Matches(std::string_view line) const override;

 private:
  std::function<bool(std::string_view)> predicate_;
};

class Basename : public Filter {
 public:
  Basename(std::string match, bool case_sensitive);

  bool Matches(std::string_view line) const override;

 private:
  std::unique_ptr<Exact> predicate_;
};

}  // namespace filter
}  // namespace pkgfile
