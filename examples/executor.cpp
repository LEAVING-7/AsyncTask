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
      [](async::InlineExecutor& e, async::Reactor& r) -> Task<> {
        e.spawnDetach(
            [](async::Reactor& r) -> Task<> {
              for (int i = 0; i < 10; i++) {
                co_await r.sleep(1s);
                LOG_INFO("hi");
              }
              co_return;
            }(r),
            r);

        for (int i = 0; i < 10; i++) {
          e.spawnDetach(
              [](async::InlineExecutor& e, async::Reactor& r, int i) -> Task<> {
                auto str = co_await e.blockSpawn([i]() -> char const* {
                  std::this_thread::sleep_for(1s);
                  LOG_INFO("wake up at :{}", i);
                  return "hello";
                });
                LOG_INFO("there: {}", str);
              }(e, r, i),
              r);
        }
        co_return;
      }(e, r),
      r);
  auto done = std::chrono::steady_clock::now();
  LOG_INFO("elapsed: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(done - now).count());
  LOG_INFO("main thread end, with return valuea: {}", gCount);
}
