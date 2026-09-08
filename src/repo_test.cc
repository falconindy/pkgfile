#include "repo.hh"

#include <unistd.h>

#include <filesystem>
#include <format>
#include <fstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace fs = std::filesystem;

namespace {

TEST(RepoTest, RepoNameFromCacheFile) {
  EXPECT_EQ(RepoNameFromCacheFile("extra.files"), "extra");
  EXPECT_EQ(RepoNameFromCacheFile("core.files"), "core");
  EXPECT_EQ(RepoNameFromCacheFile(".files"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra.files.000"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile("extra"), std::nullopt);
  EXPECT_EQ(RepoNameFromCacheFile(".db_version"), std::nullopt);
}

class ParseArchitectureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = (fs::temp_directory_path() /
             std::format("pkgfile_repo_test_{}.conf", getpid()))
                .string();
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove(path_, ec);
  }

  std::string ParseArchitecture(std::string_view architecture_line) {
    std::ofstream(path_) << "[options]\n" << architecture_line << "\n";

    AlpmConfig config;
    AlpmConfig::LoadFromFile(path_.c_str(), &config);
    return config.architecture;
  }

  std::string path_;
};

TEST_F(ParseArchitectureTest, SingleValue) {
  EXPECT_EQ(ParseArchitecture("Architecture = x86_64"), "x86_64");
}

TEST_F(ParseArchitectureTest, Auto) {
  EXPECT_EQ(ParseArchitecture("Architecture = auto"), "");
}

TEST_F(ParseArchitectureTest, MultiValueTakesFirst) {
  // Recent pacman allows space-delimited fallback architectures; only the
  // first should be used.
  EXPECT_EQ(ParseArchitecture("Architecture = x86_64 x86_64_v3"), "x86_64");
}

TEST_F(ParseArchitectureTest, AutoWithFallbackLevelsStaysAuto) {
  // Regression test: "auto x86_64_v3" was being parsed as a literal
  // architecture named "auto" instead of triggering uname()-based
  // detection, breaking every repo download for anyone using pacman's
  // psABI feature-level fallback syntax.
  EXPECT_EQ(ParseArchitecture("Architecture = auto x86_64_v3"), "");
}

}  // namespace
