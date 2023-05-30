#pragma once
#include "Task.hpp"
#include "ThreadSafe.hpp"
#include "concepts.hpp"
#include <cassert>
#include <list>
namespace async {
namespace detail {
template <typename PrimitiveTy>
struct PendingAwaiter {
  PrimitiveTy& mThis;
  auto await_ready() const noexcept { return false; }
  auto await_suspend(std::coroutine_handle<> h) noexcept { mThis.mPending.push(std::move(h)); }
  auto await_resume() const noexcept {}
};
} // namespace detail

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
      co_await detail::PendingAwaiter<Mutex>(*this);
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
  template <typename T>
  friend struct detail::PendingAwaiter;
  mpmc::Queue<std::coroutine_handle<>> mPending; // TODO: use atomic linked list
  std::atomic<bool> mLocked = false;
  ExecutorTy& mExecutor;
};
} // namespace async