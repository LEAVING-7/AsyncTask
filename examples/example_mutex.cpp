#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;
using namespace async;

struct RT {
  std::unique_ptr<MultiThreadExecutor> e;
  std::unique_ptr<Reactor> r;
};

auto case1(RT& rt) -> void
{
  static int cnt = 0;
  static auto mt = Mutex(*rt.e);
  auto now = std::chrono::steady_clock::now();
  rt.e->block(
      [](RT& rt) -> Task<> {
        for (int i = 0; i < 1'000'000; i++) {
          rt.e->spawnDetach(
              [](RT& rt) -> Task<> {
                co_await mt.lock();
                cnt += 1;
                if (cnt % 100000 == 0) {
                  printf("cnt: %d\n", cnt);
                }
                mt.unlock();
                co_return;
              }(rt),
              *rt.r);
        }
        co_return;
      }(rt),
      *rt.r);
  auto after = std::chrono::steady_clock::now();
  printf("time elapse %ldms\n", std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count());
  printf("cnt = %d\n", cnt);
}

auto normal(RT& rt)
{
  static int cnt = 0;
  auto now = std::chrono::steady_clock::now();
  for (int i = 0; i < 1'000'000; i++) {
    rt.e->spawnDetach(
        [](RT& rt) -> Task<> {
          cnt += 1;
          if (cnt % 100000 == 0) {
            printf("cnt: %d\n", cnt);
          }
          co_return;
        }(rt),
        *rt.r);
  }
  auto after = std::chrono::steady_clock::now();
  printf("time elapse %ldms\n", std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count());
  printf("cnt = %d\n", cnt);
}

int main()
{
  auto rt = RT {std::make_unique<MultiThreadExecutor>(4), std::make_unique<Reactor>()};
  case1(rt);
  normal(rt);
}