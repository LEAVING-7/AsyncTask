#include "log.hpp"
#include <barrier>
#include <chrono>
using namespace std::chrono_literals;

int main()
{
  // auto cnt = std::thread::hardware_concurrency();
  // auto barrier = std::barrier {cnt};
  // auto pool = io::ThreadPool {};
  // for (auto i = 0; i < cnt; i++) {
  //   pool.addWork(
  //       [i, &barrier]() {
  //         LOG_INFO("===ThreadId {} started", i);
  //         auto n = rand() % 5;
  //         LOG_INFO("===ThreadId {} sleep for {}s!", i, n);
  //         std::this_thread::sleep_for(n * 1s);
  //         LOG_INFO("===ThreadId {} wake up!", i);
  //       },
  //       std::nullopt);
  // }
}