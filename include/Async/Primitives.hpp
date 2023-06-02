#pragma once
#include "Async/Executor.hpp"
#include "Async/Task.hpp"
#include "Async/ThreadSafe.hpp"
#include "Async/concepts.hpp"
#include <cassert>
#include <list>
namespace async {
class Mutex {
public:
  Mutex(MultiThreadExecutor& e) : mExecutor(e) {};
  [[nodiscard]] auto lock() -> Task<void>
  {
    bool expected = false;
    if (mLocked.compare_exchange_strong(expected, true)) { // locked
      co_return;                                           // held the lock
    } else {
      struct PendingAwaiter {
        Mutex& mThis;
        auto await_ready() const noexcept { return false; }
        auto await_suspend(std::coroutine_handle<> h) noexcept { mThis.mPending.push(std::move(h)); }
        auto await_resume() const noexcept {}
      };
      co_await PendingAwaiter {*this};
      assert(mLocked.compare_exchange_strong(expected, true));
    }
  }
  auto unlock() -> void
  {
    auto value = mPending.pop();
    if (value.has_value()) {
      mExecutor.execute(value.value());
    } else {
      mLocked = false;
    }
  }
  [[nodiscard]] auto try_lock() -> bool
  {
    bool expected = false;
    if (mLocked.compare_exchange_strong(expected, true)) {
      return true;
    } else {
      return false;
    }
  }

private:
  mpmc::Queue<std::coroutine_handle<>> mPending; // TODO: use atomic linked list
  std::atomic<bool> mLocked = false;
  MultiThreadExecutor& mExecutor;
};

class CondVar {
public:
  CondVar(MultiThreadExecutor& e) : mExecutor(e) {};
  [[nodiscard]] auto wait() -> Task<void>
  {
    struct PendingAwaiter {
      CondVar& mThis;
      auto await_ready() const noexcept { return false; }
      auto await_suspend(std::coroutine_handle<> h) noexcept { mThis.mPending.push(std::move(h)); }
      auto await_resume() const noexcept {}
    };
    co_await PendingAwaiter {*this};
  }
  auto notify_one() -> bool
  {
    auto value = mPending.pop();
    if (value.has_value()) {
      mExecutor.execute(value.value());
      return true;
    } else {
      return false;
    }
  }
  auto notify_all() -> void
  {
    while (auto value = mPending.pop()) {
      mExecutor.execute(value.value());
    }
  }
private:
  mpmc::Queue<std::coroutine_handle<>> mPending;
  MultiThreadExecutor& mExecutor;
};
} // namespace async