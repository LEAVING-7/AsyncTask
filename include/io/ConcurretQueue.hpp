#pragma once
#include "predefined.hpp"
#include <atomic>
#include <condition_variable>
#include <queue>

namespace io {
template <typename T>
  requires std::is_move_assignable_v<T>
class Queue {
public:
  void push(T&& item)
  {
    {
      std::scoped_lock guard(mMutex);
      mQueue.push(std::move(item));
    }
    mCond.notify_one();
  }

  bool try_push(T const& item)
  {
    {
      std::unique_lock lock(mMutex, std::try_to_lock);
      if (!lock) {
        return false;
      }
      mQueue.push(item);
    }
    mCond.notify_one();
    return true;
  }

  bool pop(T& item)
  {
    std::unique_lock lock(mMutex);
    mCond.wait(lock, [&]() { return !mQueue.empty() || mStop; });
    if (mQueue.empty()) {
      return false;
    }
    item = std::move(mQueue.front());
    mQueue.pop();
    return true;
  }

  bool try_pop(T& item)
  {
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock || mQueue.empty()) {
      return false;
    }

    item = std::move(mQueue.front());
    mQueue.pop();
    return true;
  }

  // non-blocking pop an item, maybe pop failed.
  // predict is an extension pop condition, default is null.
  bool try_pop_if(T& item, bool (*predict)(T&) = nullptr)
  {
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (!lock || mQueue.empty()) {
      return false;
    }

    if (predict && !predict(mQueue.front())) {
      return false;
    }

    item = std::move(mQueue.front());
    mQueue.pop();
    return true;
  }

  std::size_t size() const
  {
    std::scoped_lock guard(mMutex);
    return mQueue.size();
  }

  bool empty() const
  {
    std::scoped_lock guard(mMutex);
    return mQueue.empty();
  }

  void stop()
  {
    {
      std::scoped_lock guard(mMutex);
      mStop = true;
    }
    mCond.notify_all();
  }

private:
  std::queue<T> mQueue;
  bool mStop = false;
  mutable std::mutex mMutex;
  std::condition_variable mCond;
};
} // namespace io