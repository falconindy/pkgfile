#include "repo.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(RepoTest, RepoNameFromCacheFile) {
  EXPECT_EQ(RepoNameFromCacheFile("extra.files"), "extra");
  EXPECT_EQ(RepoNameFromCacheFile("core.files"), "core");
  EXPECT_EQ(RepoNameFromCacheFile(".files"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra.files.000"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile(".db_version"), std::nullopt);
}

}  // namespace
