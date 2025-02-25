#include "filter.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(DirectoryFilterTest, MatchesDirectories) {
  pkgfile::filter::Directory filter;

  EXPECT_TRUE(filter.Matches("/bin/"));
  EXPECT_FALSE(filter.Matches("/bin"));
  EXPECT_FALSE(filter.Matches(""));
}

TEST(BinaryFilterTest, MatchesBinaries) {
  std::vector<std::string> bins{
      "/bin",     "/sbin",           "/usr/sbin",
      "/usr/bin", "/some/other/bin", "/some/other/sbin",
  };

  pkgfile::filter::Bin filter(bins);

  EXPECT_TRUE(filter.Matches("/bin/foo"));
  EXPECT_TRUE(filter.Matches("/sbin/foo"));
  EXPECT_TRUE(filter.Matches("/usr/bin/foo"));
  EXPECT_TRUE(filter.Matches("/usr/sbin/foo"));
  EXPECT_TRUE(filter.Matches("/some/other/bin/foo"));
  EXPECT_TRUE(filter.Matches("/some/other/sbin/foo"));
  EXPECT_FALSE(filter.Matches("/abin/foo"));
  EXPECT_FALSE(filter.Matches("/bin/"));
  EXPECT_FALSE(filter.Matches("/sbin/"));
  EXPECT_FALSE(filter.Matches("/abin"));
  EXPECT_FALSE(filter.Matches("/abin/"));
  EXPECT_FALSE(filter.Matches("/bin/foo/"));
  EXPECT_FALSE(filter.Matches("/sbin/foo/"));
}

TEST(NotFilterTest, MatchesNegation) {
  pkgfile::filter::Not filter(std::make_unique<pkgfile::filter::Directory>());

  EXPECT_FALSE(filter.Matches("/bin/"));
  EXPECT_TRUE(filter.Matches("/bin"));
}

TEST(RegexFilterTest, CompilesRegex) {
  EXPECT_NE(nullptr, pkgfile::filter::Regex::Compile("some.*regex", true));
  EXPECT_EQ(nullptr, pkgfile::filter::Regex::Compile("*invalid", true));
}

TEST(RegexFilterTest, MatchesByRegex) {
  {
    auto filter = pkgfile::filter::Regex::Compile("some.*regex", true);

    EXPECT_TRUE(filter->Matches("some goofy regex"));
    EXPECT_FALSE(filter->Matches("someegex"));
    EXPECT_FALSE(filter->Matches("SOME goofy REgex"));
  }

  {
    auto filter = pkgfile::filter::Regex::Compile("some.*regex", false);

    EXPECT_TRUE(filter->Matches("some goofy regex"));
    EXPECT_FALSE(filter->Matches("someegex"));
    EXPECT_TRUE(filter->Matches("SOME goofy REgex"));
  }
}

TEST(AndFilterTest, MatchesByComposite) {
  auto regex_filter = pkgfile::filter::Regex::Compile("some.*regex.*", true);
  auto regex = regex_filter.get();

  auto directory_filter = std::make_unique<pkgfile::filter::Directory>();
  auto directory = directory_filter.get();

  pkgfile::filter::And filter(std::move(regex_filter),
                              std::move(directory_filter));

  {
    // Matches both
    std::string input = "some.regex/";
    EXPECT_TRUE(regex->Matches(input));
    EXPECT_TRUE(directory->Matches(input));
    EXPECT_TRUE(filter.Matches(input));
  }

  {
    // Matches regex, not directory
    std::string input = "some.regex";
    EXPECT_TRUE(regex->Matches(input));
    EXPECT_FALSE(directory->Matches(input));
    EXPECT_FALSE(filter.Matches(input));
  }

  {
    // Matches directory, not regex
    std::string input = "some.rege/";
    EXPECT_FALSE(regex->Matches(input));
    EXPECT_TRUE(directory->Matches(input));
    EXPECT_FALSE(filter.Matches(input));
  }
}

TEST(ExactFilter, MatchesByExactCaseSensitive) {
  pkgfile::filter::Exact filter("derp", true);

  EXPECT_TRUE(filter.Matches("derp"));
  EXPECT_FALSE(filter.Matches("derpp"));
  EXPECT_FALSE(filter.Matches("dderp"));
  EXPECT_FALSE(filter.Matches("DERP"));
}

TEST(ExactFilter, MatchesByExactCaseInensitive) {
  pkgfile::filter::Exact filter("derp", false);

  EXPECT_TRUE(filter.Matches("derp"));
  EXPECT_FALSE(filter.Matches("derpp"));
  EXPECT_FALSE(filter.Matches("dderp"));
  EXPECT_TRUE(filter.Matches("DERP"));
}

TEST(BasenameFilter, MatchesByBasenameCaseSensitive) {
  pkgfile::filter::Basename filter("derp", true);

  EXPECT_TRUE(filter.Matches("derp"));
  EXPECT_TRUE(filter.Matches("/bin/derp"));
  EXPECT_FALSE(filter.Matches("dErp"));
  EXPECT_FALSE(filter.Matches("/bin/DERP"));
  EXPECT_FALSE(filter.Matches("/bin/derpp"));
}

TEST(BasenameFilter, MatchesByBasenameCaseInsensitive) {
  pkgfile::filter::Basename filter("deRp", false);

  EXPECT_TRUE(filter.Matches("derp"));
  EXPECT_TRUE(filter.Matches("/bin/derp"));
  EXPECT_TRUE(filter.Matches("dErp"));
  EXPECT_FALSE(filter.Matches("/bin/derpp"));
  EXPECT_TRUE(filter.Matches("/bin/DERP"));
}

}  // namespace
