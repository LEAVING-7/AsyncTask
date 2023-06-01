#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <optional>
namespace async::mpmc {
template <typename Lock>
concept is_lockable = requires(Lock&& lock) {
                        lock.lock();
                        lock.unlock();
                        {
                          lock.try_lock()
                          } -> std::convertible_to<bool>;
                      };

template <typename T, typename Lock = std::mutex>
  requires is_lockable<Lock>
class Queue {
public:
  using value_type = T;
  using size_type = typename std::deque<T>::size_type;

  Queue() = default;

  void push(T&& value)
  {
    std::lock_guard lock(mMutex);
    mData.push_back(std::forward<T>(value));
  }

  [[nodiscard]] bool empty() const
  {
    std::lock_guard lock(mMutex);
    return mData.empty();
  }

  [[nodiscard]] std::optional<T> pop()
  {
    std::lock_guard lock(mMutex);
    if (mData.empty()) {
      return std::nullopt;
    }
    auto front = std::move(mData.front());
    mData.pop_front();
    return front;
  }

  [[nodiscard]] std::optional<T> steal()
  {
    std::lock_guard lock(mMutex);
    if (mData.empty()) {
      return std::nullopt;
    }
    auto back = std::move(mData.back());
    mData.pop_back();
    return back;
  }

private:
  std::deque<T> mData {};
  mutable Lock mMutex {};
};
} // namespace async::mpmc