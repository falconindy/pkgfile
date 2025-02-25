#include <condition_variable>
#include <mutex>
#include <queue>

// A simple thread safe queue type that support multiple readers and writers.
template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;

  void enqueue(T data) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      q_.push(data);
    }
    cv_.notify_one();
  }

  T dequeue() {
    std::unique_lock l(mu_);
    cv_.wait(l, [this] { return !q_.empty(); });
    T data = q_.front();
    q_.pop();
    return data;
  }

  bool try_dequeue(T& data) {
    std::lock_guard lock(mu_);
    if (q_.empty()) {
      return false;
    }
    data = q_.front();
    q_.pop();
    return true;
  }

  bool empty() const {
    std::lock_guard lock(mu_);
    return q_.empty();
  }

  size_t size() const {
    std::lock_guard lock(mu_);
    return q_.size();
  }

 private:
  std::queue<T> q_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
};
