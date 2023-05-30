#include "Async/Executor.hpp"
#include "Async/Primitives.h"
#include "Async/Task.hpp"
#include <chrono>

namespace async {
auto GetReactor() -> Reactor&
{
  static auto r = Reactor {};
  return r;
}
auto GetExecutor() -> MultiThreadExecutor&
{
  static auto e = MultiThreadExecutor {4};
  return e;
}
} // namespace async
using namespace async;
int main()
{
  static auto e = async::MultiThreadExecutor {4};
  static auto r = async::Reactor {};

  static auto semaphore = async::Mutex();
  static auto cnt = size_t(0);
  auto now = std::chrono::steady_clock::now();
  e.block([]() -> Task<> {
    for (int i = 0; i < 1'000'000; i++) {
      e.spawnDetach(
          []() -> Task<> {
            co_await semaphore.acquire();
            cnt += 1;
            std::cout << cnt << '\n';
            semaphore.release();
            co_return;
          }(),
          r);
      co_return;
    }
  }());
  auto after = std::chrono::steady_clock::now();
  std::cout << "time elapse " << std::chrono::duration_cast<std::chrono::milliseconds>(after - now) << '\n';
  std::cout << cnt << '\n';
}
