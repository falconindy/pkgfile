#include "queue.hh"

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace {

TEST(ThreadSafeQueueTest, TryDequeueOnEmptyReturnsNullopt) {
  ThreadSafeQueue<int> queue;
  EXPECT_EQ(queue.try_dequeue(), std::nullopt);
}

TEST(ThreadSafeQueueTest, EnqueueThenDequeueIsFifo) {
  ThreadSafeQueue<int> queue;
  queue.enqueue(1);
  queue.enqueue(2);
  queue.enqueue(3);

  EXPECT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue.try_dequeue(), 1);
  EXPECT_EQ(queue.try_dequeue(), 2);
  EXPECT_EQ(queue.try_dequeue(), 3);
  EXPECT_EQ(queue.try_dequeue(), std::nullopt);
  EXPECT_EQ(queue.size(), 0u);
}

TEST(ThreadSafeQueueTest, DrainingPastEmptyKeepsReturningNullopt) {
  ThreadSafeQueue<int> queue;
  queue.enqueue(42);

  EXPECT_EQ(queue.try_dequeue(), 42);
  EXPECT_EQ(queue.try_dequeue(), std::nullopt);
  EXPECT_EQ(queue.try_dequeue(), std::nullopt);
}

TEST(ThreadSafeQueueTest, MovesElementsRatherThanCopies) {
  ThreadSafeQueue<std::unique_ptr<int>> queue;
  queue.enqueue(std::make_unique<int>(7));

  auto item = queue.try_dequeue();
  ASSERT_TRUE(item.has_value());
  ASSERT_NE(*item, nullptr);
  EXPECT_EQ(**item, 7);
}

// Mirrors how SearchRepoChunks uses the queue: fully populate it, then start
// the workers. Every item must be consumed exactly once, and no worker may
// block forever once the queue drains. This is the regression test for the
// check-then-blocking-dequeue race that could hang the worker pool.
TEST(ThreadSafeQueueTest, ConcurrentDrainConsumesEveryItemExactlyOnce) {
  constexpr int kItems = 100000;

  ThreadSafeQueue<int> queue;
  for (int i = 0; i < kItems; ++i) {
    queue.enqueue(int{i});
  }

  const unsigned num_workers = std::max(2u, std::thread::hardware_concurrency());
  std::vector<std::atomic<int>> seen(kItems);
  std::atomic<int> total_consumed{0};

  std::vector<std::thread> workers;
  workers.reserve(num_workers);
  for (unsigned w = 0; w < num_workers; ++w) {
    workers.emplace_back([&] {
      while (auto item = queue.try_dequeue()) {
        ++seen[*item];
        ++total_consumed;
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  EXPECT_EQ(total_consumed.load(), kItems);
  EXPECT_EQ(queue.size(), 0u);
  for (int i = 0; i < kItems; ++i) {
    EXPECT_EQ(seen[i].load(), 1) << "item " << i << " consumed wrong number of "
                                << "times";
  }
}

// Producers and consumers running concurrently must not lose or duplicate
// items.
TEST(ThreadSafeQueueTest, ConcurrentProducersAndConsumers) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kPerProducer = 25000;
  constexpr int kTotal = kProducers * kPerProducer;

  ThreadSafeQueue<int> queue;
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<bool> done_producing{false};

  std::vector<std::thread> threads;

  for (int p = 0; p < kProducers; ++p) {
    threads.emplace_back([&] {
      for (int i = 0; i < kPerProducer; ++i) {
        queue.enqueue(1);
        ++produced;
      }
    });
  }

  for (int c = 0; c < kConsumers; ++c) {
    threads.emplace_back([&] {
      while (true) {
        if (auto item = queue.try_dequeue()) {
          consumed += *item;
        } else if (done_producing.load()) {
          // One more attempt to cover the gap between the last enqueue and the
          // flag being observed.
          if (auto last = queue.try_dequeue()) {
            consumed += *last;
          } else {
            break;
          }
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  for (int p = 0; p < kProducers; ++p) {
    threads[p].join();
  }
  done_producing = true;
  for (int c = kProducers; c < kProducers + kConsumers; ++c) {
    threads[c].join();
  }

  EXPECT_EQ(produced.load(), kTotal);
  EXPECT_EQ(consumed.load(), kTotal);
  EXPECT_EQ(queue.size(), 0u);
}

}  // namespace
