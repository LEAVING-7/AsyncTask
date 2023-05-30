#pragma once
#include "Executor.hpp"
#include "Task.hpp"
#include "ThreadSafe.hpp"
#include "concepts.hpp"
#include <cassert>
#include <list>
namespace async {
#ifndef AsyncTask_GLOBAL_EXECUTOR
template <typename ExecutorTy>
class Mutex {
public:
  Mutex(ExecutorTy& e) : mExecutor(e) {};
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
  ExecutorTy& mExecutor;
};

#else
class Mutex {
public:
  static_assert(std::is_same_v<std::remove_cvref_t<std::invoke_result_t<decltype(GetExecutor)>>, MultiThreadExecutor>,
                "InlineExecutor does not require the use of Mutex.");
  Mutex() = default;
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
      GetExecutor().execute(value.value());
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
};
#endif
} // namespace async