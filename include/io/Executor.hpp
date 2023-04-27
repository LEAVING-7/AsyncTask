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

class Executor;
template <typename T>
struct BlockOnCleanUp {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    Executor* e {nullptr};
    FinalAwaiter(Executor* e) : e(e) {}
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void;
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;
    Executor* e {nullptr};
    T value;

    auto get_return_object() -> BlockOnCleanUp { return BlockOnCleanUp {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter(e); }
    auto return_value(std::pair<Executor*, T>&& pair) -> void
    {
      e = pair.first;
      value = std::forward<T>(pair.second);
    }
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit BlockOnCleanUp(coroutine_handle_type handle) : coHandle(handle) {}
  coroutine_handle_type coHandle {nullptr};
};

template <>
struct BlockOnCleanUp<void> {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    Executor* e {nullptr};
    FinalAwaiter(Executor* e) : e(e) {}
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void;
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;
    Executor* e {nullptr};
    auto get_return_object() -> BlockOnCleanUp { return BlockOnCleanUp {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter(e); }
    auto return_value(Executor* in) -> void { e = in; }
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit BlockOnCleanUp(coroutine_handle_type handle) : coHandle(handle) {}
  coroutine_handle_type coHandle {nullptr};
};

struct SpawnCleanUp {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;
  struct FinalAwaiter {
    Executor* e {nullptr};
    FinalAwaiter(Executor* e) : e(e) {}
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void;
  };
  struct promise_type {
    std::exception_ptr exceptionPtr;
    Executor* e {nullptr};
    auto get_return_object() -> SpawnCleanUp { return SpawnCleanUp {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter(e); }
    auto return_value(Executor* in) -> void { e = in; }
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit SpawnCleanUp(coroutine_handle_type handle) : coHandle(handle) {}
  coroutine_handle_type coHandle {nullptr};
};

class Executor {
public:
  friend struct BlockOnCleanUp<void>;
  template <typename T>
  friend struct BlockOnCleanUp;
  friend struct SpawnCleanUp;
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
    auto spawnTask = [](Task<> task, Executor* e) -> SpawnCleanUp {
      co_await task;
      co_return e;
    }(std::move(task), this);
    auto handle = spawnTask.coHandle;
    // LOG_INFO("spawn task 1: {}", handle.address());
    mThreadPool.push_task([handle, this]() mutable { handle.resume(); });
  }

  template <typename T>
  auto blockOn(Task<T> task) -> T
  {
    mBlockOnReturnValue.store(nullptr);
    mBlockOnDone.store(false);
    auto blockOnTask = [](Task<T> task, Executor* e) -> BlockOnCleanUp<T> {
      if constexpr (std::is_void_v<T>) {
        co_await task;
        co_return e;
      } else {
        auto result = co_await task;
        co_return {e, std::move(result)};
      }
    }(std::move(task), this);
    mThreadPool.push_task([handle = blockOnTask.coHandle] { handle.resume(); });

    LOG_INFO("Before wait");

    mBlockOnDone.wait(false);
    LOG_INFO("After wait, with tasks: {}", mThreadPool.task_total());
    // while (mBlockOnReturnValue != 0) {
    mBlockOnReturnValue.wait(nullptr);
    // }
    mThreadPool.wait_for_tasks();
    while (mSpawnedTaskCount != mSpawnedTaskDoneCount) {
      // LOG_INFO("After wait, with tasks: {}", mThreadPool.task_total());
      LOG_INFO("wait for tasks: {}/{}", mSpawnedTaskDoneCount, mSpawnedTaskCount);
    }
    if constexpr (std::is_void_v<T>) {
      assert(mBlockOnReturnValue == (void*)(1));
      return;
    } else {
      auto ptr = reinterpret_cast<T*>(mBlockOnReturnValue.load());
      auto result = std::move(*ptr);
      delete ptr;
      return result;
    }
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
    // LOG_WARN("reg task with idx: {}, fd: {}", idx, fd);
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
            exe.mThreadPool.push_task([co]() mutable { co.resume(); });
          }
        }
      }
    }
  }

  std::atomic_bool mBlockOnDone {false};
  std::atomic<void*> mBlockOnReturnValue {nullptr};

  std::atomic_size_t mSpawnedTaskCount;
  std::atomic_size_t mSpawnedTaskDoneCount;

  Poller mPoller;
  thread_pool_light mThreadPool {8};
  // EventSlab mEventSlab;
  std::jthread mEventThread;
};

template <typename T>
auto BlockOnCleanUp<T>::FinalAwaiter::await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void
{
  assert(handle.done());
  assert(e);
  e->mBlockOnReturnValue = new T {std::move(handle.promise().value)};
  LOG_INFO("set block on return value");
  e->mBlockOnDone.exchange(true);
  e->mBlockOnDone.notify_one();
  LOG_INFO("notify block on done");
  handle.destroy();
}
inline auto BlockOnCleanUp<void>::FinalAwaiter::await_suspend(std::coroutine_handle<promise_type> handle) noexcept
    -> void
{
  assert(handle.done());
  assert(e);
  e->mBlockOnReturnValue = (void*)(1);
  LOG_INFO("set block on return value");
  e->mBlockOnDone.exchange(true);
  e->mBlockOnDone.notify_one();
  LOG_INFO("notify block on done");
  handle.destroy();
}
inline auto SpawnCleanUp::FinalAwaiter::await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void
{
  assert(handle.done());
  e->mSpawnedTaskDoneCount += 1;
  handle.destroy();
  assert(e);
}
} // namespace io