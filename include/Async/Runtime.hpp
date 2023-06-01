#pragma once
#include "Executor.hpp"
#include "Task.hpp"
#include "concepts.hpp"
#include <cassert>
#include <coroutine>
#include <memory>
#include <mutex>
namespace async {
class Reactor;

template <ExecutorCpt ExecutorTy>
struct Runtime {
  template <typename... Args>
  static inline auto Init(Args&&... args) -> bool
  {
    std::call_once(mOnceFlag, [&]() {
      reactor = std::make_unique<Reactor>();
      executor = std::make_unique<ExecutorTy>(std::forward<Args>(args)...);
    });
    return true;
  }
  static inline auto GetReactor() -> Reactor&
  {
    assert(reactor);
    return *reactor;
  }
  static inline auto GetExecutor() -> ExecutorTy&
  {
    assert(executor);
    return *executor;
  }

  static inline auto SpawnDetach(Task<> task) -> void { GetExecutor().spawnDetach(std::move(task)); }
  template <typename T>
  static inline auto BlockSpawn(Task<T> task) -> T
  {
    return GetExecutor().blockSpawn(std::move(task));
  }
  template <typename T>
  static inline auto Block(Task<T> task) -> T
  {
    return GetExecutor().block(std::move(task));
  }
  static inline auto Sleep(auto duration) -> Task<> { return GetReactor().sleep(duration); }

private:
  static inline std::once_flag mOnceFlag;
  static inline std::unique_ptr<Reactor> reactor = nullptr;
  static inline std::unique_ptr<ExecutorTy> executor = nullptr;
};
} // namespace async