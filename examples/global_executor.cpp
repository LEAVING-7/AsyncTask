#define AsyncTask_GLOBAL_EXECUTOR
#include "Async/Executor.hpp"
namespace async {
auto GetReactor() -> Reactor&
{
  static auto onceFlag = std::once_flag {};
  static auto r = std::optional<Reactor> {};
  std::call_once(onceFlag, []() { r.emplace(); });
  return r.value();
}
auto GetExecutor() -> ExecutorCpt auto&
{
  static auto onceFlag = std::once_flag {};
  static auto r = std::optional<MultiThreadExecutor> {};
  std::call_once(onceFlag, []() { r.emplace(4); });
  return r.value();
}
} // namespace async
#include "Async/Task.hpp"
using namespace std::chrono_literals;
int main()
{
  auto static number = 0;
  auto now = std::chrono::steady_clock::now();
  async::GetExecutor().block([]() -> async::Task<> {
    for (int i = 0; i < 1'000'000; i++) { // 1M
      async::GetExecutor().spawnDetach([](int i) -> async::Task<> {
        number += 1;
        if (number % 1000 == 0) {
          printf("%d\n", number);
        }
        co_return;
      }(i));
    }
    puts("done");
    co_return;
  }());
  auto done = std::chrono::steady_clock::now();
  std::cout << "time elapse " << std::chrono::duration_cast<std::chrono::milliseconds>(done - now) << '\n';
  assert(number == 1'000'000); // assert failed
  printf("number : %d\n", number);
}
