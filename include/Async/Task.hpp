#pragma once
#include <cassert>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <functional>
#include <utility>

template <typename T = void>
class Task;

struct PromiseBase {
  std::coroutine_handle<> continueHandle;
  std::exception_ptr exceptionPtr;

  struct FinalAwaiter {
    auto await_ready() const noexcept -> bool { return false; }
    template <typename PromiseTy>
    auto await_suspend(std::coroutine_handle<PromiseTy> handle) noexcept -> std::coroutine_handle<>
    {
      assert(handle.done() && "handle should done here");
      if (handle.promise().continueHandle) {
        return handle.promise().continueHandle;
      } else {
        return std::noop_coroutine();
      }
    }
    auto await_resume() noexcept -> void {}
  };

  auto initial_suspend() noexcept -> std::suspend_always { return {}; }
  auto final_suspend() noexcept -> FinalAwaiter { return {}; }
  auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  auto setContinue(std::coroutine_handle<> continuation) noexcept -> void { continueHandle = continuation; }
};

template <typename T>
struct Promise final : PromiseBase {
  T returnValue;

  auto get_return_object() noexcept -> Task<T>;
  auto return_value(T value) noexcept -> void { returnValue = std::move(value); }
  auto result() const& -> T const&
  {
    if (exceptionPtr) {
      std::rethrow_exception(exceptionPtr);
    }
    return returnValue;
  }
  auto result() && -> T&&
  {
    if (exceptionPtr) {
      std::rethrow_exception(exceptionPtr);
    }
    return std::move(returnValue);
  }
};

template <>
struct Promise<void> : PromiseBase {
  auto get_return_object() noexcept -> Task<void>;
  auto return_void() noexcept -> void {}
  auto result() const -> void
  {
    if (exceptionPtr) {
      std::rethrow_exception(exceptionPtr);
    }
    return;
  }
};

template <typename T>
class Task {
public:
  using promise_type = Promise<T>;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;
  using value_type = T;

  Task() noexcept = default;
  explicit Task(std::coroutine_handle<promise_type> handle) noexcept : mHandle(handle) {}
  Task(Task const&) = delete;
  Task(Task&& other) noexcept : mHandle(std::exchange(other.mHandle, nullptr)) {}
  ~Task() noexcept
  {
    if (mHandle) {
      assert(mHandle.done() && "handle should done here");
      mHandle.destroy();
    }
  }

  auto operator==(Task const& other) -> bool { return mHandle == other.mHandle; }
  auto done() const noexcept -> bool { return mHandle.done(); }
  auto handle() const noexcept -> std::coroutine_handle<promise_type> { return mHandle; }
  auto resume() const -> bool
  {
    if (mHandle != nullptr && !mHandle.done()) {
      mHandle.resume();
      return true;
    }
    return false;
  }
  auto promise() & -> promise_type& { return mHandle.promise(); }
  auto promise() const& -> promise_type const& { return mHandle.promise(); }
  auto promise() && -> promise_type&& { return std::move(mHandle.promise()); }
  auto take() noexcept -> std::coroutine_handle<promise_type> { return std::exchange(mHandle, nullptr); }
  auto destroy() noexcept -> void
  {
    if (mHandle != nullptr) {
      mHandle.destroy();
      mHandle = nullptr;
    }
  }

  auto operator co_await() const& noexcept
  {
    struct Awaiter {
      std::coroutine_handle<promise_type> callee;
      auto await_ready() -> bool { return false; }
      auto await_suspend(std::coroutine_handle<> caller) -> std::coroutine_handle<>
      {
        callee.promise().setContinue(caller);
        return callee;
      }
      auto await_resume() -> decltype(auto) { return callee.promise().result(); }
    };
    return Awaiter {mHandle};
  }

  auto operator co_await() && noexcept
  {
    struct Awaiter {
      std::coroutine_handle<promise_type> callee;
      auto await_ready() -> bool { return false; }
      auto await_suspend(std::coroutine_handle<> caller) -> std::coroutine_handle<>
      {
        callee.promise().setContinue(caller);
        return callee;
      }
      auto await_resume() -> decltype(auto) { return std::move(callee.promise()).result(); }
    };
    return Awaiter {mHandle};
  }

private:
  std::coroutine_handle<promise_type> mHandle {nullptr};
};

template <typename T>
inline auto Promise<T>::get_return_object() noexcept -> Task<T>
{
  return Task<T> {std::coroutine_handle<Promise>::from_promise(*this)};
}
inline auto Promise<void>::get_return_object() noexcept -> Task<void>
{
  return Task<void> {std::coroutine_handle<Promise>::from_promise(*this)};
}

template <typename T>
struct DetachTask {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void
    {
      auto fn = std::move(handle.promise().afterCleanUpFn);
      auto value = std::move(handle.promise().value);
      assert(handle.done());
      handle.destroy();
      if (fn) {
        fn(std::move(value));
      }
    }
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;
    T value;
    std::function<void(T&&)> afterCleanUpFn {nullptr};

    auto get_return_object() -> DetachTask { return DetachTask {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter {}; }
    auto return_value(T in) -> void { value = std::forward<T>(in); }
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit DetachTask(coroutine_handle_type in) : handle(in) {}
  auto afterDestroy(std::function<void(T&&)>&& fn) -> DetachTask
  {
    handle.promise().afterCleanUpFn = std::move(fn);
    return *this;
  };
  coroutine_handle_type handle {nullptr};
};

template <>
struct DetachTask<void> {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void
    {
      auto fn = std::move(handle.promise().afterCleanUpFn);
      assert(handle.done());
      handle.destroy();
      if (fn) {
        fn();
      }
    }
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;
    std::function<void(void)> afterCleanUpFn {nullptr};

    auto get_return_object() -> DetachTask { return DetachTask {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter {}; }
    auto return_void() -> void {}
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit DetachTask(coroutine_handle_type in) : handle(in) {}
  auto afterDestroy(std::function<void(void)>&& fn) -> DetachTask
  {
    handle.promise().afterCleanUpFn = std::move(fn);
    return *this;
  };
  coroutine_handle_type handle {nullptr};
};

struct ContinueTask {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    auto await_ready() noexcept -> bool { return false; }
    auto await_resume() noexcept -> void {}
    auto await_suspend(std::coroutine_handle<promise_type> in) noexcept -> std::coroutine_handle<>
    {
      if (in.promise().continueHandle) {
        auto handle = in.promise().continueHandle;
        assert(in.done());
        in.destroy();
        return handle;
      } else {
        return std::noop_coroutine();
      }
    }
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;
    std::coroutine_handle<> continueHandle {nullptr};

    auto get_return_object() -> ContinueTask { return ContinueTask {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return FinalAwaiter {}; }
    auto return_void() -> void {}
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };

  explicit ContinueTask(coroutine_handle_type in) : handle(in) {}
  auto setContinue(std::coroutine_handle<> in) -> ContinueTask
  {
    handle.promise().continueHandle = in;
    return *this;
  };
  coroutine_handle_type handle {nullptr};
};