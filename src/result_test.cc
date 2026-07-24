#include "result.hh"

#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace pkgfile {
namespace {

// Print() writes directly to stdout via printf, so tests observe it by
// capturing stdout rather than inspecting Result's private state.
std::string CapturedPrint(Result* result, size_t prefixlen, char eol) {
  testing::internal::CaptureStdout();
  result->Print(prefixlen, eol);
  return testing::internal::GetCapturedStdout();
}

TEST(ResultTest, AddTracksMaxPrefixlen) {
  Result result;
  result.Add("short", "");
  result.Add("a-longer-prefix", "");
  result.Add("mid", "");

  EXPECT_EQ(result.MaxPrefixlen(), std::string("a-longer-prefix").size());
}

TEST(ResultTest, MergeFromEmptyBatchIsNoOp) {
  Result result;
  Result::Batch batch;

  result.MergeFrom(&batch);

  EXPECT_TRUE(result.Empty());
  EXPECT_EQ(result.MaxPrefixlen(), 0u);
}

TEST(ResultTest, MergeFromMovesLinesAndUpdatesMaxPrefixlen) {
  Result result;
  Result::Batch batch;
  batch.Add("core/pacman", "/usr/bin/pacman");
  batch.Add("extra/a-much-longer-package-name", "/usr/bin/thing");

  result.MergeFrom(&batch);

  EXPECT_FALSE(result.Empty());
  EXPECT_EQ(result.MaxPrefixlen(),
            std::string("extra/a-much-longer-package-name").size());

  const std::string output =
      CapturedPrint(&result, result.MaxPrefixlen(), '\n');
  EXPECT_NE(output.find("core/pacman"), std::string::npos);
  EXPECT_NE(output.find("extra/a-much-longer-package-name"), std::string::npos);
}

TEST(ResultTest, MergeFromLeavesBatchEmptyForReuse) {
  Result result;
  Result::Batch batch;
  batch.Add("first", "");
  result.MergeFrom(&batch);

  // Merging the same (now-empty) batch again must not duplicate "first".
  result.MergeFrom(&batch);
  batch.Add("second", "");
  result.MergeFrom(&batch);

  const std::string output = CapturedPrint(&result, 0, '\n');
  std::istringstream lines(output);
  std::string line;
  int count = 0;
  while (std::getline(lines, line)) {
    count++;
  }
  EXPECT_EQ(count, 2);
}

TEST(ResultTest, ConcurrentMergesLoseNoEntries) {
  Result result;
  constexpr int kThreads = 8;
  constexpr int kEntriesPerThread = 500;

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&result, t] {
      Result::Batch batch;
      for (int i = 0; i < kEntriesPerThread; ++i) {
        batch.Add("repo/pkg-" + std::to_string(t) + "-" + std::to_string(i),
                  "/some/file");
      }
      result.MergeFrom(&batch);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  const std::string output =
      CapturedPrint(&result, result.MaxPrefixlen(), '\n');
  std::istringstream lines(output);
  std::string line;
  int count = 0;
  while (std::getline(lines, line)) {
    count++;
  }
  EXPECT_EQ(count, kThreads * kEntriesPerThread);
}

}  // namespace
}  // namespace pkgfile

// vim: set ts=2 sw=2 et:
