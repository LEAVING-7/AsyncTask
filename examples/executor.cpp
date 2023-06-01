#include "Async/Executor.hpp"
#include "Async/Primitives.hpp"
#include "Async/Task.hpp"
using namespace std::chrono_literals;
int main()
{
  using RuntimeTy = async::Runtime<async::MultiThreadExecutor>;
  RuntimeTy::Init(4);

  auto static number = 0;
  auto now = std::chrono::steady_clock::now();
  auto static mt = async::Mutex();
  RuntimeTy::Block([]() -> async::Task<> {
    for (int i = 0; i < 1'000'000; i++) { // 1M
      RuntimeTy::SpawnDetach([](int i) -> async::Task<> {
        co_await mt.lock();
        number += 1;
        if (number % 1000 == 0) {
          printf("number = %d\n", number);
        }
        mt.unlock();
        co_return;
      }(i));
    }
    puts("done");
    co_return;
  }());
  auto done = std::chrono::steady_clock::now();
  printf("time elapse %ld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(done - now).count());
  assert(number == 1'000'000);
  std::cout << number << '\n';
}
