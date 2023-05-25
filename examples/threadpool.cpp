#include <atomic>
std::atomic_size_t gCnt = 0;
#include "Async/Queue.hpp"
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;
#include "Async/Executor.hpp"
static auto device = std::random_device {};

int main()
{
  {
    auto now = std::chrono::steady_clock::now();
    auto pool = async::StealingThreadPool {4};
    std::atomic_uint32_t cnt = {10'00};
    for (int i = 0; i < 10'00; i++) {
      pool.execute([&]() -> async::DetachTask<void> {
        // sleep for random time
        auto time = 1ms * (rand() % 100);
        std::this_thread::sleep_for(time);
        gCnt += 1;
        co_return;
      }()
                                .afterDestroy([&] { cnt -= 1; })
                                .handle);
    }
    while (cnt > 0) {
      std::this_thread::yield();
    }
    auto after = std::chrono::steady_clock::now();
    assert(gCnt == 1000);
    std::cout << "time: " << std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count() << "ms\n";
    puts("fuck me");
  }
  {
    auto now = std::chrono::steady_clock::now();
    auto pool = async::ThreadPool {4};
    std::atomic_uint32_t cnt = {10'00};
    for (int i = 0; i < 10'00; i++) {
      pool.execute([&]() -> async::DetachTask<void> {
        // sleep for random time
        auto time = 1ms * (rand() % 100);
        std::this_thread::sleep_for(time);
        gCnt += 1;
        co_return;
      }()
                                .afterDestroy([&] { cnt -= 1; })
                                .handle);
    }
    while (cnt > 0) {
      std::this_thread::yield();
    }
    auto after = std::chrono::steady_clock::now();
    assert(gCnt == 2000);
    std::cout << "time: " << std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count() << "ms\n";
    puts("fuck me");
  }
}