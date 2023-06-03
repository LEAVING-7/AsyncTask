#include <Async/Executor.hpp>
#include <Async/ThreadSafe.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;
std::atomic_size_t gCnt = 0;
constexpr auto TASK_COUNT = 1'000'000;
int main()
{
  {
    auto now = std::chrono::steady_clock::now();
    auto pool = async::StealingThreadPool {4};
    std::atomic_uint32_t cnt = {TASK_COUNT};
    for (int i = 0; i < TASK_COUNT; i++) {
      pool.execute([&]() -> async::DetachTask<void> {
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
    assert(gCnt == TASK_COUNT);
    printf("time: %ldms\n", std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count());
  }
  {
    auto now = std::chrono::steady_clock::now();
    auto pool = async::ThreadPool {4};
    std::atomic_uint32_t cnt = {TASK_COUNT};
    for (int i = 0; i < TASK_COUNT; i++) {
      pool.execute([&]() -> async::DetachTask<void> {
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
    assert(gCnt == TASK_COUNT * 2);
    printf("time: %ldms\n", std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count());
  }
}