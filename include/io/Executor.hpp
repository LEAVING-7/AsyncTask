#pragma once
#include "ConcurrentQueue.hpp"
#include "Reactor.hpp"
#include "Slab.hpp"
#include "async/Task.hpp"
#include "log.hpp"
#include <concepts>
#include <deque>
#include <future>
#include <queue>
#include <random>
#include <shared_mutex>

namespace io {
class State {
public:
  friend class Runner;
  friend class Executor;

  State() : mGlobal(), mLocalQueuesMt(), mLocals(), mWaitingMt(), mWaiting(), mHandleCount(0) {}

  auto isEmpty() -> bool { return isGlobalEmpty() && isLocalsEmpty(); }
  auto waitEmpty() -> void
  {
    auto lk = std::unique_lock(mDoneMt);
    mDone.wait(lk, [this]() { return mHandleCount == 0; });
  }
  auto execute(std::coroutine_handle<> handle) -> void
  {
    incCount();
    mGlobal.emplace(handle);
    mWaiting.notify_all();
  }

private:
  auto incCount() -> void
  {
    auto lk = std::scoped_lock(mDoneMt);
    ++mHandleCount;
  }
  auto decCount() -> void
  {
    auto lk = std::scoped_lock(mDoneMt);
    --mHandleCount;
    if (mHandleCount == 0 && isGlobalEmpty() && isLocalsEmpty()) {
      mDone.notify_all();
    }
  }
  auto isLocalsEmpty() -> bool
  {
    auto lk = std::shared_lock(mLocalQueuesMt);
    return std::all_of(mLocals.begin(), mLocals.end(), [](auto const& q) { return q->empty(); });
  }
  auto isGlobalEmpty() -> bool { return mGlobal.empty(); }

  spmc::ConcurrentQueue<std::coroutine_handle<>> mGlobal;
  std::shared_mutex mLocalQueuesMt;
  std::vector<std::shared_ptr<spmc::ConcurrentQueue<std::coroutine_handle<>>>> mLocals;

  std::mutex mDoneMt;
  std::condition_variable_any mDone;
  std::atomic_size_t mHandleCount;

  std::mutex mWaitingMt;
  std::condition_variable_any mWaiting;
};

class Runner {
public:
  Runner(State& state) : mState(state), mLocal(), mTicks(0)
  {
    mLocal = std::make_shared<spmc::ConcurrentQueue<std::coroutine_handle<>>>();
    mState.mLocalQueuesMt.lock();
    mState.mLocals.push_back(mLocal);
    mState.mLocalQueuesMt.unlock();

    mThread = std::jthread([this](std::stop_token tok) { run(tok); });
  }
  ~Runner() { mThread.get_stop_source().request_stop(); }

  auto acquire() -> std::coroutine_handle<>
  {
    if (auto h = mLocal->pop(); h) {
      return h.value();
    } else if (auto h = mState.mGlobal.steal(); h) {
      Steal(mState.mGlobal, *mLocal);
      return h.value();
    } else {
      auto lk = std::unique_lock(mState.mLocalQueuesMt);

      auto n = mState.mLocals.size();
      static auto rd = std::random_device();
      auto idst = std::uniform_int_distribution<size_t>(0, n);
      auto start = idst(rd);

      for (size_t i = 0; i < n * 2; ++i) {
        auto idx = (start + i) % n;
        auto& q = mState.mLocals[idx];
        if (q == mLocal) {
          continue;
        }
        Steal(*q, *mLocal);
        if (auto h = mLocal->pop(); h) {
          return h.value();
        }
      }
    }
    return {nullptr};
  }

  auto run(std::stop_token tok) -> void
  {
    while (!tok.stop_requested()) {
      auto lk = std::unique_lock(mState.mWaitingMt);
      mState.mWaiting.wait(lk, tok, [this]() { return !mState.mGlobal.empty(); });
      if (auto h = acquire(); h) {
        h.resume();
        mState.decCount();
      } else {
        continue;
      }
      // resume each handle in local queue
      while (auto h = mLocal->pop()) {
        if (h.has_value()) {
          h.value().resume();
        } else {
          break;
        }
      }
      auto ticks = mTicks.fetch_add(1);
      if (ticks % 64 == 0) {
        Steal(mState.mGlobal, *mLocal);
      }
    }
  }

private:
  State& mState;
  std::shared_ptr<spmc::ConcurrentQueue<std::coroutine_handle<>>> mLocal;
  std::atomic_size_t mTicks;
  std::jthread mThread;
};

class ThreadPool {
public:
  ThreadPool(size_t n) : mState()
  {
    for (size_t i = 0; i < n; ++i) {
      auto runner = std::make_shared<Runner>(mState);
      mRunners.push_back(runner);
    }
  }
  auto execute(std::coroutine_handle<> handle) -> void { mState.execute(handle); }
  auto waitEmpty() -> void { mState.waitEmpty(); }
  auto isEmpty() -> bool { return mState.isEmpty(); }

private:
  State mState;
  std::vector<std::shared_ptr<Runner>> mRunners;
};

class MutilThreadExecutor {
public:
  MutilThreadExecutor(size_t n) : mPool(n) {}

  auto spawn(Task<> in, io::Reactor& reactor) -> void
  {
    mSpawnCount.fetch_add(1, std::memory_order_acquire);
    auto afterDoneFn = [this, &reactor]() {
      mSpawnCount.fetch_sub(1, std::memory_order_release);
      reactor.notify();
    };
    auto handle = [this](Task<> task)
        -> DetachTask<void> { co_return co_await task; }(std::move(in)).afterDestroy(afterDoneFn).handle;
    mPool.execute(handle);
  }

  template <typename T>
  [[nodiscard]] auto block(Task<T> in, io::Reactor& reactor) -> T
  {
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();
    if constexpr (std::is_void_v<T>) {
      auto afterDoneFn = [this, promise, &reactor]() {
        promise->set_value();
        reactor.notify();
      };
      auto handle = [](Task<T> task)
          -> DetachTask<T> { co_return co_await task; }(std::move(in)).afterDestroy(std::move(afterDoneFn)).handle;
      mPool.execute(handle);
    } else {
      auto afterDoneFn = [promise, &reactor](auto&& value) {
        promise->set_value(std::forward<T>(value));
        reactor.notify();
      };
      auto handle = [](Task<T> task) -> DetachTask<T> {
        auto value = co_await task;
        co_return value;
      }(std::move(in))
                                            .afterDestroy(std::move(afterDoneFn))
                                            .handle;
      mPool.execute(handle);
    }

    using namespace std::chrono_literals;
    while (true) {
      if (future.wait_for(0s) == std::future_status::ready) {
        if (mSpawnCount.load(std::memory_order_acquire) == 0) {
          LOG_CRITICAL("everything done");
          break;
        }
      }
      reactor.lock().react(std::nullopt, mPool);
    }
    return future.get();
  }

private:
  std::atomic_size_t mSpawnCount;
  ThreadPool mPool;
};

class InlineExecutor {
public:
  InlineExecutor() : mSpawnCount(0), mQueue() {}
  auto spawn(Task<> task, io::Reactor& reactor) -> void
  {
    mSpawnCount += 1;
    auto afterDestroyFn = [this, &reactor]() {
      reactor.notify();
      mSpawnCount -= 1;
    };
    auto handle = [this](Task<> task) -> DetachTask<void> { co_return co_await task; }(std::move(task))
                                             .afterDestroy(std::move(afterDestroyFn))
                                             .handle;
    mQueue.push(handle);
  }

  template <typename T>
    requires(not std::is_void_v<T>)
  auto block(Task<T> task, io::Reactor& reactor) -> T
  {
    auto returnValue = std::optional<T>(std::nullopt);
    auto afterDestroyFn = [&reactor, &returnValue](T&& value) {
      returnValue.emplace(std::move(value));
      reactor.notify();
    };
    auto handle = [this](Task<T> task)
        -> DetachTask<T> { co_return co_await task; }(std::move(task)).afterDestroy(std::move(afterDestroyFn)).handle;
    handle.resume();

    while (true) {
      while (!mQueue.empty()) {
        auto handle = mQueue.front();
        mQueue.pop();
        handle.resume();
      }
      if (mSpawnCount == 0 && returnValue.has_value() && mQueue.empty()) {
        break;
      }
      reactor.lock().react(std::nullopt, *this);
    }

    return std::move(returnValue.value());
  }
  auto block(Task<> task, io::Reactor& reactor) -> void
  {
    auto hasValue = false;
    auto afterDestroyFn = [&reactor, &hasValue]() {
      hasValue = true;
      reactor.notify();
    };
    auto handle = [this](Task<> task) -> DetachTask<void> { co_return co_await task; }(std::move(task))
                                             .afterDestroy(std::move(afterDestroyFn))
                                             .handle;
    handle.resume();

    while (true) {
      while (!mQueue.empty()) {
        auto handle = mQueue.front();
        mQueue.pop();
        handle.resume();
      }
      if (mSpawnCount == 0 && hasValue && mQueue.empty()) {
        break;
      }
      reactor.lock().react(std::nullopt, *this);
    }
  }

  auto execute(std::coroutine_handle<> handle) -> void { handle.resume(); }

private:
  std::queue<std::coroutine_handle<>> mQueue;
  size_t mSpawnCount;
};


auto constexpr DEFAULT_MAX_THREAD = 500;
auto constexpr MIN_MAX_THREAD = 1;
auto constexpr MAX_MAX_THREAD = 10000;

class BlockingThreadPool {
public:
  auto schedule(std::coroutine_handle<> handle) -> void
  {
    auto lk = std::unique_lock(mMt);
    mQueue.push_back(std::move(handle));
    mCv.notify_one();
    growPool();
  }

private:
  auto threadLoop() -> void
  {
    using namespace std::chrono_literals;
    auto lk = std::unique_lock(mMt);
    while (true) {
      mIdleCount -= 1;
      while (!mQueue.empty()) {
        growPool();
        auto task = std::move(mQueue.front());
        mQueue.pop_front();
        lk.unlock();
        task.resume();
        lk.lock();
      }
      mIdleCount += 1;
      auto r = mCv.wait_for(lk, 500ms);
      if (r == std::cv_status::timeout && mQueue.empty()) {
        mIdleCount -= 1;
        mThreadCount -= 1;
        break;
      }
    }
  }

  auto growPool() -> void
  {
    assert(!mMt.try_lock());
    while (mQueue.size() > mIdleCount * 5 && mThreadCount < mThreadLimits) {
      mThreadCount += 1;
      mIdleCount += 1;
      mCv.notify_all();
      std::thread([this]() { threadLoop(); }).detach();
    }
  }

  std::mutex mMt;
  std::condition_variable mCv;

  size_t mIdleCount;
  size_t mThreadCount;
  std::deque<std::coroutine_handle<>> mQueue;
  size_t mThreadLimits;
};
} // namespace io