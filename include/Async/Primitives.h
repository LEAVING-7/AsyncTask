#pragma once

#include "Task.hpp"
#include <atomic>
#include <cassert>
#include <mutex>
namespace async {
template <typename ExecutorType>
class Mutex {
public:
  Mutex(ExecutorType& e) : mPool(e) {};
  [[nodiscard]] auto lock() -> Task<void>
  {
    struct Awaiter {
      Mutex& mThis;
      auto await_ready() const noexcept { return false; }
      auto await_suspend(std::coroutine_handle<> h) noexcept
      {
        auto lk = std::scoped_lock(mThis.mHandlesMutex);
        mThis.mHandles.push_back(h);
      }
      auto await_resume() const noexcept { }
    };
    bool expected = false;
    if (mLocked.compare_exchange_strong(expected, true)) { // locked
      co_return;                                           // held the lock
    } else {
      co_await Awaiter {*this};
      assert(mLocked.compare_exchange_strong(expected, true));
    }
  }
  auto unlock()
  {
    {
      auto lk = std::scoped_lock(mHandlesMutex);
      if (!mHandles.empty()) {
        auto back = mHandles.back();
        mHandles.pop_back();
        mPool.execute(back);
      } else {
        mLocked = false;
      }
    }
  }

private:
  ExecutorType& mPool;
  std::atomic_bool mLocked = false;
  std::mutex mHandlesMutex;
  std::vector<std::coroutine_handle<>> mHandles;
};

} // namespace async