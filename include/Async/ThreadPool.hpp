#pragma once
#include "Task.hpp"
#include "ThreadSafe.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace async {

struct ThreadPoolBase {
  virtual ~ThreadPoolBase() = default;
  virtual auto execute(std::coroutine_handle<> handle) -> void = 0;
};

class BlockingThreadPool : public ThreadPoolBase {
public:
  BlockingThreadPool(size_t threadLimit) : mIdleCount(0), mThreadCount(0), mThreadLimits(threadLimit) {}
  ~BlockingThreadPool() override
  {
    auto lk = std::unique_lock(mQueueMt);
    mQueueCv.wait(lk, [this]() { return mQueue.empty(); });
  }
  auto execute(std::coroutine_handle<> handle) -> void override
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

class ThreadPool : public ThreadPoolBase {
public:
  [[deprecated("Much slower than StealingThreadPool")]] ThreadPool(size_t threadCount)
      : mThreadCount(threadCount), mThreads(std::make_unique<std::thread[]>(threadCount))
  {
    mRunning = true;
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i] = std::thread(&ThreadPool::worker, this);
    }
  }
  ~ThreadPool() override
  {
    waitEmpty();
    mRunning = false;
    mTaskAvailableCV.notify_all();
    for (size_t i = 0; i < mThreadCount; ++i) {
      mThreads[i].join();
    }
  }
  auto execute(std::coroutine_handle<> handle) -> void override
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

class StealingThreadPool : public ThreadPoolBase {
public:
  explicit StealingThreadPool(uint32_t const& threadNum) : mQueues(threadNum)
  {
    std::size_t currentId = 0;
    for (std::size_t i = 0; i < threadNum; ++i) {
      mThreads.emplace_back([&, id = currentId](std::stop_token const& stop_tok) {
        do {
          mQueues[id].signal.acquire();
          do {
            while (auto task = mQueues[id].tasks.pop()) {
              mPendingTasks.fetch_sub(1, std::memory_order_release);
              std::invoke(std::move(task.value()));
            }
            for (std::size_t j = 1; j < mQueues.size(); ++j) {
              const std::size_t index = (id + j) % mQueues.size();
              if (auto task = mQueues[index].tasks.steal()) {
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

  ~StealingThreadPool() override
  {
    for (std::size_t i = 0; i < mThreads.size(); ++i) {
      mThreads[i].request_stop();
      mQueues[i].signal.release();
      mThreads[i].join();
    }
  }
  StealingThreadPool(StealingThreadPool const&) = delete;
  StealingThreadPool& operator=(StealingThreadPool const&) = delete;

  [[nodiscard]] auto size() const { return mThreads.size(); }
  auto execute(std::coroutine_handle<> h) -> void override
  {
    if (h == nullptr) {
      return;
    }
    enqueue_task(h);
  }

private:
  auto enqueue_task(std::coroutine_handle<> h) -> void
  {
    const std::size_t i = mCount++ % mQueues.size();
    mPendingTasks.fetch_add(1, std::memory_order_relaxed);
    mQueues[i].tasks.push(std::move(h));
    mQueues[i].signal.release();
  }

  struct TaskItem {
    mpmc::Queue<std::coroutine_handle<>> tasks {};
    std::binary_semaphore signal {0};
  };

  std::vector<std::jthread> mThreads;
  std::deque<TaskItem> mQueues;
  std::size_t mCount {};
  std::atomic_int64_t mPendingTasks {};
};
} // namespace async