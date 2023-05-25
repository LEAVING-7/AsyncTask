#include "Async/Executor.hpp"
#include "Async/Task.hpp"
using namespace std::chrono_literals;

std::atomic_size_t gCount = 0;
#include <random>
int main()
{
  auto e = async::InlineExecutor {};
  auto r = async::Reactor {};
  auto now = std::chrono::steady_clock::now();
  e.block(
      [](async::InlineExecutor& e, async::Reactor& r) -> async::Task<> {
        for (int i = 0; i < 10'000; i++) {
          e.spawnDetach(
              [](async::Reactor& r) -> async::Task<> {
                co_await r.sleep(1min);
                co_return;
              }(r),
              r);
        }
        puts("done");
        co_return;
      }(e, r),
      r);
  auto done = std::chrono::steady_clock::now();
  std::cout << "time elapse " << std::chrono::duration_cast<std::chrono::seconds>(done - now) << '\n';
}
