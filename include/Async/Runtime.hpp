#pragma once
#include "Async/Executor.hpp"
#include "Async/Task.hpp"
#include "Async/concepts.hpp"
#include <cassert>
#include <coroutine>
#include <memory>
#include <mutex>
namespace async {
class Reactor;
template <typename T = void>
struct JoinHandle;
template <ExecutorCpt ExecutorTy>
struct Runtime {
  template <typename... Args>
  static inline auto Init(Args&&... args) -> bool
  {
    std::call_once(onceFlag, [&]() {
      reactor = std::make_unique<Reactor>();
      executor = std::make_unique<ExecutorTy>(std::forward<Args>(args)...);
      isInit.store(true);
    });
    return true;
  }
  static inline auto GetReactor() -> Reactor&
  {
    if (!isInit.load()) {
      assert(false && "Runtime is not initialized or initialization failed");
    }
    return *reactor;
  }
  static inline auto GetExecutor() -> ExecutorTy&
  {
    if (!isInit.load()) {
      assert(false && "Runtime is not initialized or initialization failed");
    }
    return *executor;
  }

  static inline auto SpawnDetach(Task<> task) -> void { GetExecutor().spawnDetach(std::move(task)); }
  template <typename Fn, typename ...Args>
  [[nodiscard]] static inline auto BlockSpawn(Fn&& fn, Args&&... args)
  {
    return GetExecutor().blockSpawn(std::forward<Fn>(fn), std::forward<Args>(args)...);
  }
  template <typename T>
  static inline auto Block(Task<T> task) -> T
  {
    return GetExecutor().block(std::move(task));
  }
  [[nodiscard]] static inline auto Sleep(TimePoint::duration duration) { return GetReactor().sleep(duration); }
  template <typename T>
  static auto Spawn(JoinHandle<T>& handle) -> void
  {
    return GetExecutor().spawn(handle);
  }
  template <typename... JoinHandleTy>
  [[nodiscard]] static auto WaitAll(JoinHandleTy&&... handles) -> Task<>
  {
    (..., co_await handles.join());
  }

private:
  static inline std::atomic_bool isInit = false;
  static inline std::once_flag onceFlag;
  static inline std::unique_ptr<Reactor> reactor = nullptr;
  static inline std::unique_ptr<ExecutorTy> executor = nullptr;
};
} // namespace async