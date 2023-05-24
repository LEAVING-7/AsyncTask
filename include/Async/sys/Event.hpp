#pragma once
#include "Async/utils/predefined.hpp"
#include <atomic>
#include <limits>


#ifdef __linux__
  #include "Async/sys/unix/epoll.hpp"
#endif

namespace async {
static constexpr auto NOTIFY_KEY = std::numeric_limits<size_t>::max();
struct Event {
  size_t key;
  bool readable;
  bool writable;

  static auto All(size_t key) -> Event;
  static auto Readable(size_t key) -> Event;
  static auto Writable(size_t key) -> Event;
  static auto None(size_t key) -> Event;

  auto isNotifyEvent() -> bool { return key == NOTIFY_KEY && readable && !writable; }
  auto isTimerEvent() -> bool { return key == NOTIFY_KEY && !readable && !writable; }
};

// static constexpr auto STOP_KEY = NOTIFY_KEY - 1;

enum class PollMode {
  Oneshot,
  Level,
  Edge,
  EdgeOneshot,
};

class Poller {
public:
  Poller() : mEvents(), mEventsLock(), mNotified(false)
  {
    auto r = impl::Poller::Create();
    assert(r.has_value());
    mPoller = std::move(r.value());
  };
  ~Poller() = default;
  constexpr auto supportEdge() const -> bool { return mPoller.supportEdge(); }
  constexpr auto supportLevel() const -> bool { return mPoller.supportLevel(); }

  auto add(int fd, Event ev) -> StdResult<void> { return mPoller.add(fd, ev, PollMode::Oneshot); }
  auto add(int fd, Event ev, PollMode mode) -> StdResult<void>
  {
    if (ev.key == NOTIFY_KEY) {
      return make_unexpected(std::errc::invalid_argument);
    } else {
      return mPoller.add(fd, ev, mode);
    }
  }
  auto mod(int fd, Event ev) -> StdResult<void> { return mPoller.mod(fd, ev, PollMode::Oneshot); }
  auto mod(int fd, Event ev, PollMode mode) -> StdResult<void>
  {
    if (ev.key == NOTIFY_KEY) {
      return make_unexpected(std::errc::invalid_argument);
    } else {
      return mPoller.mod(fd, ev, mode);
    }
  }
  auto del(int fd) -> StdResult<void> { return mPoller.del(fd); }

  auto wait(std::vector<Event>& events, std::optional<std::chrono::nanoseconds> timeout) -> StdResult<size_t>
  {
    auto t = mEventsLock.try_lock();
    if (t) {
      auto r = mPoller.wait(mEvents, timeout);
      if (!r) {
        mEventsLock.unlock();
        return make_unexpected(r.error());
      }
      mNotified.exchange(false);
      auto len = events.size();
      for (auto const& e : mEvents) {
        if (e.key != NOTIFY_KEY) {
          events.push_back(e);
        }
      }
      mEventsLock.unlock();
      return {events.size() - len};
    } else {
      // other thread is waiting, just return 0
      return {0};
    }
  }
  auto notify() -> StdResult<void>
  {
    if (!mNotified.exchange(true)) {
      return mPoller.notify();
    } else {
      return {};
    }
  }

  // private:
  impl::Poller mPoller;
  impl::Events mEvents;
  std::mutex mEventsLock;
  std::atomic_bool mNotified;
};
} // namespace async