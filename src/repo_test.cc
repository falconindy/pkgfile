#include "repo.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(RepoTest, RepoNameFromCacheFile) {
  EXPECT_EQ(RepoNameFromCacheFile("extra.pfdb"), "extra");
  EXPECT_EQ(RepoNameFromCacheFile("core.pfdb"), "core");
  EXPECT_EQ(RepoNameFromCacheFile(".pfdb"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra.pfdb.000"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile(".db_version"), std::nullopt);
}

}  // namespace
