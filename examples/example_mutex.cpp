#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;
using namespace async;
using RT = Runtime<MultiThreadExecutor>;

auto case1() -> void
{
  RT::Init(8);
  auto now = std::chrono::high_resolution_clock::now();
  static int cnt = 0;
  RT::Block([]() -> Task<> {
    auto mutex = std::make_shared<Mutex>();
    for (int i = 0; i < 1'000'000; i++) {
      RT::SpawnDetach([](auto mt) -> Task<> {
        co_await mt->lock();
        cnt++;
        if (cnt % 100000 == 0) {
          printf("cnt: %d\n", cnt);
        }
        mt->unlock();
        co_return;
      }(mutex));
    }
    co_return;
  }());
  auto end = std::chrono::high_resolution_clock::now();
  printf("time: %ld\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count());
  printf("cnt: %d\n", cnt);
  assert(cnt == 1'000'000);
}

auto case2() -> void
{
  RT::Init(8);
  auto now = std::chrono::high_resolution_clock::now();
  static int cnt = 0;
  RT::Block([]() -> Task<> {
    for (int i = 0; i < 1'000'000; i++) {
      RT::SpawnDetach([]() -> Task<> {
        cnt++;
        if (cnt % 100000 == 0) {
          printf("cnt: %d\n", cnt);
        }
        co_return;
      }());
    }
    co_return;
  }());
  auto end = std::chrono::high_resolution_clock::now();
  printf("time: %ld\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count());
  printf("cnt: %d\n", cnt);
  assert(cnt != 1'000'000);
}

int main()
{
  case1();
  case2();
}