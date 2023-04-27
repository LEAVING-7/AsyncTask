/* #include "io/Executor.hpp"
#include <algorithm>

namespace io {
Executor::Executor(size_t numThread) : mPool(numThread), mPoller()
{
  mEventLoopThread = std::jthread([this](std::stop_token token) { eventLoop(token); });
}
auto Executor::eventLoop(std::stop_token token) -> void
{
  std::vector<Event> events;
  bool isNotify = false;
  while (!token.stop_requested() && !isNotify) {
    events.clear();
    auto r = mPoller.wait(events, std::nullopt);
    if (!r) {
      std::cerr << "Poller::wait failed: " << r.error().message() << std::endl;
      continue;
    }
    LOG_CRITICAL("get {} events", events.size());
    for (auto& event : events) {
      if ((event.readable || event.writable) && event.key != NOTIFY_KEY) {
        // auto lk = std::unique_lock {mSlabLock};
        // auto r = mSlab.tryRemove(event.key);
        auto r = mEventSlab.remove(event.key);
        if (!r) {
          LOG_CRITICAL("remove task failed with key: {}", event.key);
          assert(0);
          continue;
        }

        auto& taskItem = *r;
        auto& entry = mSpawnTaskContainer.get(taskItem.handle);
        mPool.push_task([&entry, this] {
          switch (entry.state()) {
          case ScheduleTask::State::Ready:
            assert(entry.resume());
            break;
          case ScheduleTask::State::Done:
            assert(entry.destroy());
            mSpawnTaskContainer.remove(entry.handle());
            break;
          case ScheduleTask::State::Running:
          case ScheduleTask::State::Destroyed:
            break;
          }
        });
      } else if (event.isNotifyEvent()) {
        LOG_INFO("get notify event");
        isNotify = true;
        break;
      }
    }
  }
  LOG_INFO("event loop thread exit");
}
} // namespace io
 */