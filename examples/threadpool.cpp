#include <atomic>
std::atomic_size_t gCnt = 0;
#include "Async/ConcurrentQueue.hpp"
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;
#include "Async/Executor.hpp"
static auto device = std::random_device {};

int main()
{
  auto now = std::chrono::steady_clock::now();
  auto pool = async::ThreadPool {4};
  static auto dist = std::uniform_int_distribution<> {1, 10};
  for (int i = 0; i < 10'00; i++) {
    pool.execute([]() -> async::Task<> {
      auto ms = std::chrono::milliseconds {dist(device)};
      std::this_thread::sleep_for(ms);
      gCnt += 1;
      co_return;
    }()
                             .take());
    // std::this_thread::sleep_for(1ms);
  }
  pool.waitEmpty();
  auto after = std::chrono::steady_clock::now();
}