#include <mutex>
#include <optional>
#include <queue>

// A simple thread safe queue type that supports multiple readers and writers.
template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;

  void enqueue(T&& data) {
    std::lock_guard lock(mu_);
    q_.push(std::move(data));
  }

  // Pops and returns the next item, or nullopt if the queue is empty. The
  // check-and-pop is atomic, so concurrent consumers can't race on the last
  // element.
  std::optional<T> try_dequeue() {
    std::lock_guard lock(mu_);
    if (q_.empty()) {
      return std::nullopt;
    }
    T data = std::move(q_.front());
    q_.pop();
    return data;
  }

  size_t size() const {
    std::lock_guard lock(mu_);
    return q_.size();
  }

 private:
  std::queue<T> q_;
  mutable std::mutex mu_;
};
