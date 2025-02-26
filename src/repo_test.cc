#include "repo.hh"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

TEST(RepoTest, FilenameHasRepoSuffix) {
  EXPECT_TRUE(FilenameHasRepoSuffix("extra.files.000"));
  EXPECT_TRUE(FilenameHasRepoSuffix("extra.files.999"));
  EXPECT_FALSE(FilenameHasRepoSuffix("extra.000"));
  EXPECT_FALSE(FilenameHasRepoSuffix("extra.files"));
  EXPECT_FALSE(FilenameHasRepoSuffix("extra.files.0"));
  EXPECT_FALSE(FilenameHasRepoSuffix("extra.files.00"));
}

}  // namespace
