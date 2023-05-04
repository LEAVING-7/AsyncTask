#pragma once
#include "Slab.hpp"
#include "ThreadPool.hpp"
#include "async/Task.hpp"
#include "io/sys/Event.hpp"
#include "log.hpp"
#include <coroutine>
#include <forward_list>
#include <list>
#include <shared_mutex>
#include <unordered_set>

using namespace std::chrono_literals;

namespace io {
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

class Executor {
public:
  Executor() : mEventThread(), mThreadPool()
  {
    mEventThread = std::jthread([this](std::stop_token tok) { eventLoop(*this, tok); });
  }
  ~Executor()
  {
    LOG_INFO("executor dtor");
    mEventThread.request_stop();
    mPoller.notify();
  };

  auto spawn(Task<> task) -> void
  {
    mSpawnedTaskCount += 1;
    int a = 233;
    auto spawnTask = [](Task<> task) -> CleanTask<void> {
      co_await task;
      co_return;
    }(std::move(task));
    spawnTask.afterDestroy([this] { mSpawnedTaskCount -= 1; });
    mThreadPool.push_task(spawnTask.handle);
  }

  template <typename T>
  auto blockOn(Task<T> task) -> T
  {
    auto promise = std::promise<T> {};
    auto future = promise.get_future();

    auto handle = std::coroutine_handle<> {nullptr};
    if constexpr (!std::is_void_v<T>) {
      auto afterDone = [this, &promise](T&& value) {
        promise.set_value(std::move(value));
        return;
      };
      auto blockOnTask = [](Task<T> task)
          -> CleanTask<T> { co_return co_await std::move(task); }(std::move(task)).afterDestroy(std::move(afterDone));
      handle = blockOnTask.handle;
    } else {
      auto afterDone = [this, &promise]() {
        promise.set_value();
        return;
      };
      auto blockOnTask = [](Task<void> task) -> CleanTask<void> { co_return co_await std::move(task); }(std::move(task))
                                                    .afterDestroy(std::move(afterDone));
      handle = blockOnTask.handle;
    }
    mThreadPool.push_task(handle);
    mThreadPool.wait_for_tasks();
    future.wait();
    while (mSpawnedTaskCount != mSpawnedTaskDoneCount) {
      LOG_INFO("wait for tasks: {}/{}", mSpawnedTaskDoneCount, mSpawnedTaskCount);
    }
    return future.get();
  }

  auto regTask(int fd, std::coroutine_handle<> handle, bool readable, bool writable) -> StdResult<void>
  {
    // auto idx = mEventSlab.add(handle, fd);
    auto event = Event {
        .key = (size_t)handle.address(),
        .readable = readable,
        .writable = writable,
    };
    if (auto r = mPoller.add(fd, event); !r) {
      if (r.error() == std::errc::file_exists) {
        // LOG_INFO("file {} exists, modify event", fd);
        auto k = mPoller.mod(fd, event);
        if (!k) {
          return make_unexpected(k.error());
        }
      } else {
        return make_unexpected(r.error());
      }
    };
    return {};
  }
  auto regReadableTask(int fd, std::coroutine_handle<> task) -> StdResult<void>
  {
    return regTask(fd, task, true, false);
  }
  auto regWritableTask(int fd, std::coroutine_handle<> task) -> StdResult<void>
  {
    return regTask(fd, task, false, true);
  }
  // auto unregTask(size_t key) -> StdResult<EventSlab::Item>
  // {
  //   auto task = mEventSlab.remove(key);
  //   if (!task) {
  //     return make_unexpected(std::make_error_code(std::errc::result_out_of_range));
  //   }
  //   if (task->fd == -1) { // spawned task
  //     return std::move(task.value());
  //   }
  //   if (auto r = mPoller.del(task->fd); !r) {
  //     return make_unexpected(r.error());
  //   };
  //   // LOG_WARN("unreg task with idx: {}, fd: {}", key, task->fd);
  //   return std::move(task.value());
  // }

private:
  static auto eventLoop(Executor& exe, std::stop_token token) -> void
  {
    auto events = std::vector<io::Event> {};
    while (!token.stop_requested()) {
      events.clear();
      auto len = exe.mPoller.wait(events, std::nullopt);
      if (len.has_value()) {
        // LOG_INFO("poller wait {} events", len.value());
      } else {
        LOG_ERROR("poller wait error: {}", len.error().message());
      }
      for (auto& e : events) {
        if (e.key == NOTIFY_KEY) {
          LOG_INFO("notify event");
          continue;
        }
        if (e.readable || e.writable) {
          auto co = std::coroutine_handle<>::from_address((void*)e.key);
          if (co == nullptr) {
            LOG_ERROR("cannot find coroutine for key {}", e.key);
          } else {
            exe.mThreadPool.push_task(co);
          }
        }
      }
    }
  }
  std::atomic_size_t mSpawnedTaskCount;
  std::atomic_size_t mSpawnedTaskDoneCount;

  Poller mPoller;
  thread_pool_light mThreadPool {8};
  std::jthread mEventThread;
};
} // namespace io