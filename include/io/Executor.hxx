/* #pragma once
#include "ConcurretQueue.hpp"
#include "Slab.hpp"
#include "ThreadPool2.hpp"
#include "async/Task.hpp"
#include "io/sys/Event.hpp"
#include "log.hpp"
#include <coroutine>
#include <forward_list>
#include <list>
#include <shared_mutex>
#include <unordered_map>
using namespace std::chrono_literals;


// promise type
struct ScheduleTask {
  enum class State { Ready, Running, Done, Destroyed };

  ScheduleTask() : mState(State::Destroyed), mHandle(nullptr) {};
  ScheduleTask(std::coroutine_handle<> mHandle) : mState(State::Ready), mHandle(mHandle) {}

  auto resume() -> bool
  {
    auto expected = State::Ready;
    if (mState.compare_exchange_strong(expected, State::Running)) {
      mHandle.resume();
      if (mHandle.done()) {
        mState.store(State::Done);
      } else {
        mState.store(State::Ready);
      }
      return true;
    } else {
      return false;
    }
  }

  auto destroy() -> bool
  {
    auto expected = State::Done;
    if (mState.compare_exchange_strong(expected, State::Destroyed)) {
      mHandle.destroy();
      return true;
    } else {
      return false;
    }
  }
  auto is(State state) const -> bool { return mState == state; }
  auto state() const -> State { return mState; }
  auto address() const -> void* { return (mHandle.address()); }
  auto handle() const -> std::coroutine_handle<> { return mHandle; }

private:
  std::atomic<State> mState;
  std::coroutine_handle<> mHandle;
};

inline auto to_string(ScheduleTask::State state) -> std::string_view
{
  switch (state) {
  case ScheduleTask::State::Ready:
    return "Ready";
  case ScheduleTask::State::Running:
    return "Running";
  case ScheduleTask::State::Done:
    return "Done";
  case ScheduleTask::State::Destroyed:
    return "Destroyed";
  }
}

class SpawnTaskContainer {
public:
  SpawnTaskContainer() = default;
  ~SpawnTaskContainer() = default;

  auto add(std::coroutine_handle<> handle) -> void
  {
    auto lk = std::unique_lock {mLock};
    mScheduleTasks.emplace(handle.address(), handle);
  }
  auto remove(std::coroutine_handle<> handle) -> void
  {
    auto lk = std::unique_lock {mLock};
    mScheduleTasks.erase(handle.address());
  }
  auto get(std::coroutine_handle<> handle) -> ScheduleTask&
  {
    auto lk = std::unique_lock {mLock};
    return mScheduleTasks.at(handle.address());
  }
  auto lock() -> std::unique_lock<std::shared_mutex> { return std::unique_lock {mLock}; }
  auto size() const -> size_t { return mScheduleTasks.size(); }

  auto map() -> std::unordered_map<void const*, ScheduleTask>& { return mScheduleTasks; }

private:
  std::shared_mutex mLock;
  std::unordered_map<void const*, ScheduleTask> mScheduleTasks;
};

class EventSlab {
public:
  struct Item {
    std::coroutine_handle<> handle;
    int fd;
  };
  EventSlab() = default;
  ~EventSlab() = default;

  auto add(std::coroutine_handle<> handle, int fd) -> size_t
  {
    auto lk = std::unique_lock {mLock};
    return mSlab.insert({handle, fd});
  }

  auto remove(size_t key) -> std::optional<Item>
  {
    auto lk = std::unique_lock {mLock};
    return mSlab.tryRemove(key);
  }

  auto get(size_t key) -> Item const&
  {
    auto lk = std::shared_lock {mLock};
    return *mSlab.get(key);
  }

private:
  std::shared_mutex mLock;
  Slab<Item> mSlab;
};

namespace io {
class Executor {
public:
  Executor(size_t numThread = std::thread::hardware_concurrency());
  Executor(Executor const&) = delete;
  ~Executor()
  {
    LOG_INFO("Before executor destroy, spawned task count: {}", mSpawnTaskContainer.size());
    auto r1 = mEventLoopThread.request_stop();
    auto r = mPoller.notify();
    mPool.wait_for_tasks();
    LOG_INFO("after wait");
  }
  auto spawn(Task<void>&& task) -> void
  {
    mSpawnedCount += 1;
    auto h = spawnTask(std::move(task), mSpawnedFinishedCount).yieldOwnership();
    mSpawnTaskContainer.add(h);
    mPool.push_task([this, h]() mutable -> void {
      auto& entry = mSpawnTaskContainer.get(h);
      entry.resume();
      // if (entry.destroy()) {
      //   mSpawnTaskContainer.remove(h);
      //   LOG_INFO("spawned task destroyed success");
      // }
      // LOG_ERROR("container size: {}", mSpawnTaskContainer.size());
    });
  }

  template <typename T>
  auto blockOn(Task<T>&& task) -> T
  {
    assert(mIsBlocking.test() == false && "Executor cannot block twice");
    mIsBlocking.test_and_set();
    LOG_INFO("block on enter");
    auto flag = std::atomic_flag {false};
    auto h = blockOnTask(std::move(task), flag);
    mPool.push_task([this, &h]() {
      LOG_INFO("before");
      h.resume();
      LOG_INFO("after");
    });
    while (!h.isDone()) {
      flag.wait(false);
    }
    LOG_INFO("block on exit");
    LOG_ERROR("container size: {}", mSpawnTaskContainer.size());
    LOG_INFO("spawned task count: {}, destroy task count: {}", mSpawnedCount, mSpawnedFinishedCount);
    for (auto& [k, v] : mSpawnTaskContainer.map()) {
      LOG_INFO("key: {}, state: {}", k, to_string(v.state()));
      switch (v.state()) {
      case ScheduleTask::State::Ready:
        // v.resume();
      case ScheduleTask::State::Running:
      case ScheduleTask::State::Done:
      case ScheduleTask::State::Destroyed:
        break;
      }
    }

    while (mSpawnedCount != mSpawnedFinishedCount) {
      LOG_INFO("spawned task count: {}, destroy task count: {}", mSpawnedCount, mSpawnedFinishedCount);
      LOG_INFO("container size: {}", mSpawnTaskContainer.size());
      std::this_thread::sleep_for(1s);
    }
    LOG_INFO("block on exit 2");
    mIsBlocking.clear();
    if constexpr (std::is_same_v<T, void>) {
      return;
    } else {
      return std::move(h.promise().result());
    }
  }

  auto regTask(int fd, std::coroutine_handle<> handle, bool readable, bool writable) -> StdResult<size_t>
  {
    // auto guard = mSpawnTaskContainer.lock();
    // auto it = mSpawnTaskContainer.map().find(task.handle.address());
    // if (it == mSpawnTaskContainer.map().end()) {
    //   mSpawnTaskContainer.map().emplace(task.handle.address(), task.handle);
    // } else {
    //   LOG_INFO("task already exists");
    // }
    // guard.unlock();
    auto idx = mEventSlab.add(handle, fd);
    auto event = Event {
        .key = idx,
        .readable = readable,
        .writable = writable,
    };
    if (auto r = mPoller.add(fd, event); !r) {
      if (r.error() == std::errc::file_exists) {
        LOG_INFO("file {} exists, modify event", fd);
        auto k = mPoller.mod(fd, event);
        if (!k) {
          return make_unexpected(k.error());
        } else {
          return idx;
        }
      }
      return make_unexpected(r.error());
    };
    LOG_WARN("reg task with idx: {}, fd: {}", idx, fd);
    return idx;
  }

  // return the key of the task
  auto regReadableTask(int fd, std::coroutine_handle<> task) -> StdResult<size_t>
  {
    return regTask(fd, task, true, false);
  }
  auto regWritableTask(int fd, std::coroutine_handle<> task) -> StdResult<size_t>
  {
    return regTask(fd, task, false, true);
  }
  auto unregTask(size_t key) -> StdResult<EventSlab::Item>
  {
    auto task = mEventSlab.remove(key);

    if (!task) {
      return make_unexpected(std::make_error_code(std::errc::result_out_of_range));
    }
    if (task->fd == -1) { // spawned task
      return std::move(task.value());
    }
    if (auto r = mPoller.del(task->fd); !r) {
      return make_unexpected(r.error());
    };
    LOG_WARN("unreg task with idx: {}, fd: {}", key, task->fd);
    return std::move(task.value());
  }

private:
  auto eventLoop(std::stop_token token) -> void;
  template <typename T>
  auto blockOnTask(Task<T> task, std::atomic_flag& flag) -> Task<T>
  {
    if constexpr (std::is_same_v<void, T>) {
      co_await task;
      flag.test_and_set();
      flag.notify_one();
      LOG_INFO("block on task done");
      co_return;
    } else {
      auto value = co_await task;
      flag.test_and_set();
      flag.notify_one();
      LOG_INFO("block on task done");
      co_return std::move(value);
    }
  }

  auto spawnTask(Task<> task, std::atomic_size_t& cnt) -> Task<>
  {
    co_await task;
    cnt += 1;
    co_return;
  }

  Poller mPoller;
  thread_pool_light mPool;

  EventSlab mEventSlab;
  SpawnTaskContainer mSpawnTaskContainer;

  std::jthread mEventLoopThread;
  std::atomic_flag mIsBlocking {false};
  std::atomic_size_t mSpawnedCount {0};
  std::atomic_size_t mSpawnedFinishedCount {0};
};
} // namespace io */