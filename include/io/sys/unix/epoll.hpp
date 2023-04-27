#pragma once
#include "platform.hpp"

#ifdef UNIX_PLATFORM
  #include <chrono>
  #include <memory>
  #include <predefined.hpp>
  #include <sys/epoll.h>
namespace io {
struct Event;
enum class PollMode;

namespace impl {
struct Events {
  static constexpr auto MAX_EVENTS = 1024;
  std::unique_ptr<struct epoll_event> data;
  size_t len;
  Events() : data(new struct epoll_event[MAX_EVENTS]), len(MAX_EVENTS) {}

  struct Iterator {
    Iterator(struct epoll_event* ptr) : mPtr(ptr) {}
    auto operator++() -> Iterator&
    {
      ++mPtr;
      return *this;
    }
    auto operator++(int) -> Iterator
    {
      auto tmp = *this;
      ++mPtr;
      return tmp;
    }
    auto operator--() -> Iterator&
    {
      --mPtr;
      return *this;
    }
    auto operator--(int) -> Iterator
    {
      auto tmp = *this;
      --mPtr;
      return tmp;
    }
    auto operator==(Iterator const& rhs) const -> bool { return mPtr == rhs.mPtr; }
    auto operator!=(Iterator const& rhs) const -> bool { return mPtr != rhs.mPtr; }
    auto operator*() const -> Event;
    auto operator->() const -> Event;

  private:
    struct epoll_event* mPtr;
  };

  auto begin() -> Iterator { return Iterator(data.get()); }
  auto end() -> Iterator { return Iterator(data.get() + len); }
};

class Poller {
public:
  static auto Create() -> StdResult<Poller>;
  Poller() : mEpollFd(-1), mEventFd(-1), mTimerFd(-1) {}
  Poller(int epollFd, int eventFd, int timerFd) : mEpollFd(epollFd), mEventFd(eventFd), mTimerFd(timerFd) {}
  ~Poller();
  Poller(Poller const&) = delete;
  Poller(Poller&&);
  Poller& operator=(Poller const&) = delete;
  Poller& operator=(Poller&&);

  constexpr auto supportEdge() const -> bool { return true; }
  constexpr auto supportLevel() const -> bool { return true; }

  auto add(int fd, Event ev, PollMode mode) -> StdResult<void>;
  auto mod(int fd, Event ev, PollMode mode) -> StdResult<void>;
  auto del(int fd) -> StdResult<void>;

  auto wait(Events& events, std::optional<std::chrono::milliseconds> timeout) -> StdResult<void>;
  auto notify() -> StdResult<void>;

  // private:
  int mEpollFd;
  int mEventFd;
  int mTimerFd;
};
} // namespace impl
} // namespace io

#endif