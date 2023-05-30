#pragma once
#include "Async/Reactor.hpp"
#include "Async/Slab.hpp"
#include "Async/Task.hpp"
#include "Async/ThreadSafe.hpp"
#include "Async/concepts.hpp"
#include <cassert>
#include <concepts>
#include <deque>
#include <future>
#include <queue>
#include <random>
#include <shared_mutex>
#include <source_location>
#include <thread>

namespace async {
class BlockingThreadPool {
public:
  BlockingThreadPool(size_t threadLimit) : mIdleCount(0), mThreadCount(0), mThreadLimits(threadLimit) {}
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
  }

private:
  auto loop() -> void
  {
    using namespace std::chrono_literals;
    auto lk = std::unique_lock(mQueueMt);
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

template <typename ExecutorType>
class BlockingExecutor {
public:
  template <std::invocable Fn, typename... Args>
  auto blockSpawn(Fn&& fn, Args&&... args)
  {
    std::call_once(mBlockingPoolFlag, [this]() { mBlockingPool = std::make_unique<BlockingThreadPool>(500); });
    using Result = std::invoke_result_t<Fn, Args...>;
    auto function = std::bind_front(std::forward<Fn>(fn), std::forward<Args>(args)...);
    if constexpr (std::is_void_v<Result>) {
      struct Awaiter {
        BlockingExecutor<ExecutorType>& e;
        decltype(function) fn;
        auto await_ready() -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> in) -> void
        {
          auto task = [](std::invocable auto&& fn) -> ContinueTask {
            fn();
            co_return;
          }(std::move(fn));
          e.execute(task.setContinue(in).handle);
        }
        auto await_resume() -> Result { return; }
      };
      return Awaiter {*this, std::move(function)};
    } else {
      struct Awaiter {
        BlockingExecutor<ExecutorType>& e;
        decltype(function) fn;
        std::optional<Result> result;
        auto await_ready() -> bool { return false; }
        auto await_suspend(std::coroutine_handle<> in) -> void
        {
          auto task = [this](std::invocable auto&& fn) -> ContinueTask {
            this->result = std::move(fn());
            co_return;
          }(std::move(fn));
          e.execute(task.setContinue(in).handle);
        }
        auto await_resume() -> Result
        {
          assert(result.has_value());
          return std::move(result.value());
        }
      };
      return Awaiter {*this, std::move(function)};
    }
  }
  auto execute(std::coroutine_handle<> handle) -> void { mBlockingPool->execute(handle); }

protected:
  std::once_flag mBlockingPoolFlag;
  std::unique_ptr<BlockingThreadPool> mBlockingPool {nullptr};
};

class ThreadPool {
public:
  [[deprecated("Much slower than StealingThreadPool")]] ThreadPool(size_t threadCount)
      : mThreadCount(threadCount), mThreads(std::make_unique<std::thread[]>(threadCount))
  {
    mRunning = true;
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i] = std::thread(&ThreadPool::worker, this);
    }
  }
  ~ThreadPool()
  {
    waitEmpty();
    mRunning = false;
    mTaskAvailableCV.notify_all();
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i].join();
    }
  }
  auto execute(std::coroutine_handle<> handle) -> void
  {
    if (handle == nullptr) {
      return;
    }
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
  auto worker() -> void
  {
    while (mRunning) {
      std::unique_lock<std::mutex> tasks_lock(mTaskMt);
      mTaskAvailableCV.wait(tasks_lock, [this] { return !mTasks.empty() || !mRunning; });
      if (mRunning) {
        auto handle = std::move(mTasks.front());
        mTasks.pop();
        tasks_lock.unlock();
        handle.resume();
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

class StealingThreadPool {
public:
  explicit StealingThreadPool(uint32_t const& threadNum) : mGlobalQueue(threadNum)
  {
    std::size_t currentId = 0;
    for (std::size_t i = 0; i < threadNum; ++i) {
      mThreads.emplace_back([&, id = currentId](std::stop_token const& stop_tok) {
        do {
          mGlobalQueue[id].signal.acquire();
          do {
            while (auto task = mGlobalQueue[id].tasks.pop()) {
              mPendingTasks.fetch_sub(1, std::memory_order_release);
              std::invoke(std::move(task.value()));
            }
            for (std::size_t j = 1; j < mGlobalQueue.size(); ++j) {
              const std::size_t index = (id + j) % mGlobalQueue.size();
              if (auto task = mGlobalQueue[index].tasks.steal()) {
                mPendingTasks.fetch_sub(1, std::memory_order_release);
                std::invoke(std::move(task.value()));
                break;
              }
            }
          } while (mPendingTasks.load(std::memory_order_acquire) > 0);
        } while (!stop_tok.stop_requested());
      });
      ++currentId;
    }
  }

  ~StealingThreadPool()
  {
    for (std::size_t i = 0; i < mThreads.size(); ++i) {
      mThreads[i].request_stop();
      mGlobalQueue[i].signal.release();
      mThreads[i].join();
    }
  }
  StealingThreadPool(StealingThreadPool const&) = delete;
  StealingThreadPool& operator=(StealingThreadPool const&) = delete;

  [[nodiscard]] auto size() const { return mThreads.size(); }
  auto execute(std::coroutine_handle<> h)
  {
    if (h == nullptr) {
      return;
    }
    enqueue_task(h);
  }

private:
  auto enqueue_task(std::coroutine_handle<> h) -> void
  {
    const std::size_t i = mCount++ % mGlobalQueue.size();
    mPendingTasks.fetch_add(1, std::memory_order_relaxed);
    mGlobalQueue[i].tasks.push(std::move(h));
    mGlobalQueue[i].signal.release();
  }

  struct TaskItem {
    mpmc::Queue<std::coroutine_handle<>> tasks {};
    std::binary_semaphore signal {0};
  };

  std::vector<std::jthread> mThreads;
  std::deque<TaskItem> mGlobalQueue;
  std::size_t mCount {};
  std::atomic_int_fast64_t mPendingTasks {};
};

class MultiThreadExecutor {
public:
  MultiThreadExecutor(size_t n) : mPool(n) {}
  auto spawnDetach(Task<> in, Reactor& reactor) -> void
  {
    mSpawnCount.fetch_add(1, std::memory_order_acquire);
    auto afterDoneFn = [this, &reactor]() {
      mSpawnCount.fetch_sub(1, std::memory_order_release);
      reactor.notify();
    };
    auto handle = [](Task<> task)
        -> DetachTask<void> { co_return co_await task; }(std::move(in)).afterDestroy(afterDoneFn).handle;
    mPool.execute(handle);
  }

  template <typename T>
  [[nodiscard]] auto block(Task<T> in, Reactor& reactor) -> T
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
      auto newTask = [](Task<T> task) -> DetachTask<T> {
        auto value = co_await task;
        co_return value;
      }(std::move(in));
      mPool.execute(newTask.afterDestroy(std::move(afterDoneFn)).handle);
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

  template <typename... Args>
  auto blockSpawn(Args&&... args)
  {
    return mBlockingExecutor.blockSpawn(std::forward<Args>(args)...);
  }
  auto execute(std::coroutine_handle<> handle) { mPool.execute(handle); }

private:
  BlockingExecutor<MultiThreadExecutor> mBlockingExecutor;

  std::atomic_size_t mSpawnCount;
  StealingThreadPool mPool;
};

class InlineExecutor {
public:
  InlineExecutor() : mQueue(), mSpawnCount(0) {}
  auto spawnDetach(Task<> task, Reactor& reactor) -> void
  {
    mSpawnCount += 1;
    auto afterDestroyFn = [this, &reactor]() {
      mSpawnCount -= 1;
      reactor.notify();
    };
    auto handle = [](Task<> task) -> DetachTask<void> { co_return co_await task; }(std::move(task))
                                         .afterDestroy(std::move(afterDestroyFn))
                                         .handle;
    mQueue.push(handle);
  }

  template <typename T>
    requires(not std::is_void_v<T>)
  auto block(Task<T> task, Reactor& reactor) -> T
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
  auto block(Task<> task, Reactor& reactor) -> void
  {
    auto hasValue = false;
    auto afterDestroyFn = [&reactor, &hasValue]() {
      hasValue = true;
      reactor.notify();
    };
    auto handle = [](Task<> task) -> DetachTask<void> { co_return co_await task; }(std::move(task))
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

  template <typename... Args>
  [[nodiscard]] auto blockSpawn(Args&&... args)
  {
    return mBlockingExecutor.blockSpawn(std::forward<Args>(args)...);
  }
  auto execute(std::coroutine_handle<> handle) -> void { handle.resume(); }

private:
  BlockingExecutor<InlineExecutor> mBlockingExecutor;

  std::queue<std::coroutine_handle<>> mQueue;
  size_t mSpawnCount;
};
} // namespace async
