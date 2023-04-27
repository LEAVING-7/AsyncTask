#include <iostream>

#include <chrono>
#include <coroutine>
#include <cassert>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
using namespace std::chrono_literals;

// auto t1() -> Task<std::string>
// {
//   std::cout << "t1" << std::endl;
//   co_return "233";
// }

// auto t0() -> Task<std::string>
// {
//   std::cout << "t0" << std::endl;
//   auto result = co_await t1();
//   co_await std::suspend_always {};
//   std::cout << "t0 end" << result << std::endl;
//   co_return "233";
// }

// auto f(Task<std::string> t) -> std::string
// {
//   assert(t.handle().address() != nullptr);
//   std::cout << "t.done: " << t.done() << std::endl;
//   std::cout << "t.resume: " << t.resume() << std::endl;
//   std::cout << "t.done: " << t.done() << std::endl;
//   return t.promise().result();
// }

// int main()
// {
//   auto t = t0();
//   t.resume();
//   t.resume();
//   puts("before f");
//   f(std::move(t));
//   puts("after f");
// }
#include <vector>

std::vector<std::coroutine_handle<>> mFreeHandles;

namespace co {
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
        mFreeHandles.push_back(handle);
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
  explicit Task(std::coroutine_handle<promise_type> handle) noexcept : mHandle(handle)
  {
    printf("Task::Task with handle: %p\n", handle.address());
  }
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
} // namespace co

struct CleanupTask {
  struct promise_type;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  struct FinalAwaiter {
    auto await_ready() noexcept -> bool { return false; }
    auto await_suspend(coroutine_handle_type handle) noexcept -> void
    {
      puts("CleanupTask:: final awaiter suspend");
      assert(handle.done());
      handle.destroy();
    }
    auto await_resume() noexcept -> void {}
  };

  struct promise_type {
    std::exception_ptr exceptionPtr;

    auto get_return_object() -> CleanupTask { return CleanupTask {coroutine_handle_type::from_promise(*this)}; }
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept -> FinalAwaiter { return {}; }
    auto return_void() noexcept -> void {}
    auto unhandled_exception() noexcept -> void { exceptionPtr = std::current_exception(); }
  };
  explicit CleanupTask(coroutine_handle_type handle) : mHandle(handle) {}
  coroutine_handle_type mHandle {nullptr};
};

auto echo(int c) -> co::Task<int> { co_return c; }

auto t2(int b) -> co::Task<int> { co_return co_await echo(b); }

auto t3(int a) -> CleanupTask
{
  co_await std::suspend_always {};
  auto r = co_await t2(233);
  printf("t3: %d\n", r);
  co_return;
}

auto t4(int a) -> co::Task<>
{
  co_await std::suspend_always {};
  auto t = t3(233);
  t.mHandle.resume();
  co_return;
}

#include <shared_mutex>
int main()
{
  // auto tt = t2(233).take();
  auto t = t4(233);
  while(t.resume()){ 
    
  }
  // t.mHandle.destroy();
}