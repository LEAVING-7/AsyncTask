#include "Async/Executor.hpp"
#include "Async/Primitives.h"
#include "Async/Task.hpp"
using namespace std::chrono_literals;

#include <random>
int main()
{
  auto static e = async::MultiThreadExecutor {4};
  auto static r = async::Reactor {};
  auto static number = 0;
  auto now = std::chrono::steady_clock::now();
  auto static mt = async::Mutex(e);
  e.block(
      []() -> async::Task<> {
        for (int i = 0; i < 1'000'000; i++) { // 一亿
          e.spawnDetach(
              [](async::Reactor& r, int i) -> async::Task<> {
                co_await mt.lock();
                number += 1;
                if (number % 1000 == 0) {
                  std::cout << number << '\n';
                }
                mt.unlock();
                co_return;
              }(r, i),
              r);
        }
        puts("done");
        co_return;
      }(),
      r);
  auto done = std::chrono::steady_clock::now();
  std::cout << "time elapse " << std::chrono::duration_cast<std::chrono::milliseconds>(done - now) << '\n';
  // 20694ms
  std::cout << number << '\n';
}
