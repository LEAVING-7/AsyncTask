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
class BlockingThreadPool {
public:
  BlockingThreadPool(size_t threadLimit) : mIdleCount(0), mThreadLimits(threadLimit), mThreadCount(0) {}
  ~BlockingThreadPool()
  {
    auto lk = std::unique_lock(mQueueMt);
    mQueueCv.wait(lk, [this]() { return mQueue.empty(); });
  }

  auto execute(std::coroutine_handle<> handle) -> void
  {
    auto lk = std::unique_lock(mQueueMt);
    mQueue.push_back(std::move(handle));
    mQueueCv.notify_one();
    growPool();
    LOG_INFO("execute task: {}", handle.address());
  }

private:
  auto loop() -> void
  {
    using namespace std::chrono_literals;

    LOG_INFO("new thread spawned: {}", std::this_thread::get_id());
    auto lk = std::unique_lock(mQueueMt);
    while (true) {
      mIdleCount -= 1;
      while (!mQueue.empty()) {
        growPool();
        auto task = std::move(mQueue.front());
        mQueue.pop_front();
        lk.unlock();
        LOG_INFO("resume task at: {}", std::this_thread::get_id());
        task.resume();
        lk.lock();
      }
      mIdleCount += 1;
      auto r = mQueueCv.wait_for(lk, 500ms);
      if (r == std::cv_status::timeout && mQueue.empty()) {
        mIdleCount -= 1;
        mThreadCount -= 1;
        break;
      }
    }
  }

  auto growPool() -> void
  {
    assert(!mQueueMt.try_lock());
    while (mQueue.size() > mIdleCount * 5 && mThreadCount < mThreadLimits) {
      mThreadCount += 1;
      mIdleCount += 1;
      mQueueCv.notify_all();
      std::thread([this]() { loop(); }).detach();
    }
  }

  std::mutex mQueueMt;
  std::condition_variable mQueueCv;
  std::deque<std::coroutine_handle<>> mQueue;

  size_t mIdleCount;
  size_t mThreadCount;
  size_t mThreadLimits;
};

class ThreadPool {
public:
  ThreadPool(size_t threadCount) : mThreadCount(threadCount), mThreads(std::make_unique<std::thread[]>(threadCount))
  {
    create_threads();
  }
  ~ThreadPool()
  {
    waitEmpty();
    destroy_threads();
  }
  auto execute(std::coroutine_handle<> handle) -> void
  {
    {
      auto lk = std::scoped_lock(mTaskMt);
      mTasks.push(std::move(handle));
    }
    ++mTaskTotal;
    mTaskAvailableCV.notify_one();
  }
  void waitEmpty()
  {
    mWaiting = true;
    std::unique_lock<std::mutex> tasks_lock(mTaskMt);
    mTaskDoneCV.wait(tasks_lock, [this] { return (mTaskTotal == 0); });
    mWaiting = false;
  }

private:
  auto create_threads() -> void
  {
    mRunning = true;
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i] = std::thread(&ThreadPool::worker, this);
    }
  }

  auto destroy_threads() -> void
  {
    mRunning = false;
    mTaskAvailableCV.notify_all();
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i].join();
    }
  }

  auto worker() -> void
  {
    while (mRunning) {
      std::function<void()> task;
      std::unique_lock<std::mutex> tasks_lock(mTaskMt);
      mTaskAvailableCV.wait(tasks_lock, [this] { return !mTasks.empty() || !mRunning; });
      if (mRunning) {
        task = std::move(mTasks.front());
        mTasks.pop();
        tasks_lock.unlock();
        task();
        tasks_lock.lock();
        --mTaskTotal;
        if (mWaiting) {
          mTaskDoneCV.notify_one();
        }
      }
    }
  }

  std::atomic_bool mRunning = false;
  std::condition_variable mTaskAvailableCV = {};
  std::condition_variable mTaskDoneCV = {};
  std::queue<std::coroutine_handle<>> mTasks = {};
  std::atomic_size_t mTaskTotal = 0;
  mutable std::mutex mTaskMt = {};
  size_t mThreadCount = 0;
  std::unique_ptr<std::thread[]> mThreads = nullptr;
  std::atomic_bool mWaiting = false;
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

    if constexpr (std::is_void_v<T>) { // return void
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
          break;
        }
      }
      reactor.lock().react(std::nullopt, mPool);
    }
    return future.get();
  }

  auto blockSpawn(Task<> task, io::Reactor& r)
  {
    std::call_once(mBlockingPoolFlag, [this]() { mBlockingPool = std::make_unique<BlockingThreadPool>(500); });
    struct Awaiter {
      MutilThreadExecutor& e;
      Reactor& r;
      Task<> task;
      auto await_ready() -> bool { return false; }
      auto await_suspend(std::coroutine_handle<> handle) -> void
      {
        auto continueHandle = [](MutilThreadExecutor& e, Reactor& r, Task<> task) -> ContinueTask {
          task.resume();
          co_return;
        }(e, r, std::move(task))
                                                                                         .setContinue(handle)
                                                                                         .handle;
        e.mBlockingPool->execute(continueHandle);
      }
      auto await_resume() -> void {}
    };
    return Awaiter {*this, r, std::move(task)};
  }
  template <typename T>
  auto blockSpawn(Task<T> task, io::Reactor& r)
  {
    std::call_once(mBlockingPoolFlag, [this]() { mBlockingPool = std::make_unique<BlockingThreadPool>(500); });
    struct Awaiter {
      MutilThreadExecutor& e;
      Reactor& r;
      Task<> task;
      auto await_ready() -> bool { return false; }
      auto await_suspend(std::coroutine_handle<> handle) -> void
      {
        auto continueHandle = [](MutilThreadExecutor& e, Reactor& r, Task<> task) -> ContinueTask {
          e.block(std::move(task), r);
          co_return;
        }(e, r, std::move(task))
                                                                                         .setContinue(handle)
                                                                                         .handle;
        e.mBlockingPool->execute(continueHandle);
      }
      auto await_resume() -> T {}
    };
    return Awaiter {*this, r, std::move(task)};
  }

private:
  std::once_flag mBlockingPoolFlag;
  std::unique_ptr<BlockingThreadPool> mBlockingPool {nullptr};

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
      mSpawnCount -= 1;
      reactor.notify();
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
} // namespace io
