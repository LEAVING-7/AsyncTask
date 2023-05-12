#include <atomic>
std::atomic_size_t gCnt = 0;
#include "Async/ConcurrentQueue.hpp"
#include "log.hpp"
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;
#include "Async/Executor.hpp"
static auto device = std::random_device {};

int main()
{
  auto now = std::chrono::steady_clock::now();
  auto pool = io::ThreadPool {4};
  // auto pool = io::BS::thread_pool_light {4};
  static auto dist = std::uniform_int_distribution<> {1, 10};
  for (int i = 0; i < 10'00; i++) {
    pool.execute([]() -> Task<> {
      auto ms = std::chrono::milliseconds {dist(device)};
      std::this_thread::sleep_for(ms);
      gCnt += 1;
      co_return;
    }()
                             .take());
    // std::this_thread::sleep_for(1ms);
  }
  pool.waitEmpty();
  // pool.wait_for_tasks();
  LOG_INFO("gCnt: {}", gCnt);
  auto after = std::chrono::steady_clock::now();
  LOG_INFO("time: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(after - now).count());
  // auto queue = spmc::Queue<int> {};
  // auto thiefs = std::vector<std::thread>();
  // std::thread owner([&]() {
  //   for (int i = 0; i < 1000000; i = i + 1) {
  //     queue.emplace(i);
  //   }
  // });

  // std::atomic_size_t stealCount = 0;

  // // While multiple (any) threads can steal items from the other end
  // for (int i = 0; i < 9; i++) {
  //   thiefs.push_back(std::thread([&]() {
  //     while (!queue.empty()) {
  //       std::optional item = queue.steal();
  //       if (item) {
  //         stealCount++;
  //       }
  //     }
  //   }));
  // }

  // owner.join();
  // for (auto& thief : thiefs) {
  //   thief.join();
  // }
  // LOG_INFO("queue size: {}", queue.size());
  // LOG_INFO("steal count: {}", stealCount);
}