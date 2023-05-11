#include "io/ConcurrentQueue.hpp"
#include "log.hpp"
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;

int main()
{
  auto queue = spmc::ConcurrentQueue<int> {};
  auto thiefs = std::vector<std::thread>();
  std::thread owner([&]() {
    for (int i = 0; i < 1000000; i = i + 1) {
      queue.emplace(i);
    }
  });

  std::atomic_size_t stealCount = 0;

  // While multiple (any) threads can steal items from the other end
  for (int i = 0; i < 9; i++) {
    thiefs.push_back(std::thread([&]() {
      while (!queue.empty()) {
        std::optional item = queue.steal();
        if (item) {
          stealCount++;
        }
      }
    }));
  }

  owner.join();
  for (auto& thief : thiefs) {
    thief.join();
  }
  LOG_INFO("queue size: {}", queue.size());
  LOG_INFO("steal count: {}", stealCount);
}