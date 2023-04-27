#include "io/sys/unix/epoll.hpp"
#ifdef UNIX_PLATFORM
  #include "log.hpp"

  #include <io/sys/Event.hpp>
  #include <sys/epoll.h>
  #include <sys/eventfd.h>
  #include <sys/fcntl.h>
  #include <sys/timerfd.h>
  #include <unistd.h>
namespace io::impl {
constexpr auto ReadFlags() -> uint32_t { return EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLPRI; }
constexpr auto WriteFlags() -> uint32_t { return EPOLLOUT | EPOLLHUP | EPOLLERR; }

auto Events::Iterator::operator*() const -> Event
{
  return io::Event {mPtr->data.u64, (mPtr->events & ReadFlags()) != 0, (mPtr->events & WriteFlags()) != 0};
}
auto Events::Iterator::operator->() const -> Event
{
  return io::Event {mPtr->data.u64, (mPtr->events & ReadFlags()) != 0, (mPtr->events & WriteFlags()) != 0};
}

  #define RE(r)                                                                                                        \
    if (!r) {                                                                                                          \
      return make_unexpected(r.error());                                                                               \
    }
auto Poller::Create() -> StdResult<Poller>
{
  Poller poller;
  auto r = SysCall(::epoll_create1, EPOLL_CLOEXEC);
  if (!r.has_value() && r.error() == std::errc::function_not_supported) {
    r = SysCall(::epoll_create, 1024);
    auto flag = SysCall(::fcntl, r.value(), F_GETFD);
    if (r && flag && SysCall(::fcntl, r.value(), F_SETFD, FD_CLOEXEC | flag.value()).has_value()) {
      poller.mEpollFd = r.value();
    }
    return make_unexpected(r.error());
  } else {
    poller.mEpollFd = r.value();
  }

  r = (SysCall(::eventfd, 0, EFD_CLOEXEC | EFD_NONBLOCK));
  RE(r);
  poller.mEventFd = r.value();
  r = (SysCall(::timerfd_create, CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
  RE(r);
  poller.mTimerFd = r.value();
  RE(poller.add(poller.mTimerFd, Event::None(io::NOTIFY_KEY), PollMode::Oneshot));
  RE(poller.add(poller.mEventFd, Event::Readable(io::NOTIFY_KEY), PollMode::Oneshot));
  LOG_INFO("Poller created: epoll_fd={}, event_fd={}, timer_fd={}", poller.mEpollFd, poller.mEventFd, poller.mTimerFd);
  return poller;
}

auto PollModeToEpollEvent(PollMode mode) -> struct epoll_event {
  epoll_event event;
  event.events = 0;
  switch (mode) {
  case PollMode::Oneshot:
    event.events |= EPOLLONESHOT;
    break;
  case PollMode::Level:
    event.events |= 0;
    break;
  case PollMode::Edge:
    event.events |= EPOLLET;
    break;
  case PollMode::EdgeOneshot:
    event.events |= EPOLLET | EPOLLONESHOT;
    break;
  }
  return event;
} 

auto Poller::add(int fd, Event ev, PollMode mode) -> StdResult<void>
{
  epoll_event event = PollModeToEpollEvent(mode);
  event.data.u64 = ev.key;
  if (ev.readable) {
    event.events |= ReadFlags();
  }
  if (ev.writable) {
    event.events |= WriteFlags();
  }
  RE(SysCall(::epoll_ctl, mEpollFd, EPOLL_CTL_ADD, fd, &event));
  return {};
}
auto Poller::mod(int fd, Event ev, PollMode mode) -> StdResult<void>
{
  epoll_event event = PollModeToEpollEvent(mode);
  event.data.u64 = ev.key;
  if (ev.readable) {
    event.events |= ReadFlags();
  }
  if (ev.writable) {
    event.events |= WriteFlags();
  }
  RE(SysCall(::epoll_ctl, mEpollFd, EPOLL_CTL_MOD, fd, &event));
  return {};
}
auto Poller::del(int fd) -> StdResult<void>
{
  RE(SysCall(::epoll_ctl, mEpollFd, EPOLL_CTL_DEL, fd, nullptr));
  return {};
}
auto Poller::wait(Events& events, std::optional<std::chrono::milliseconds> timeout) -> StdResult<void>
{
  auto timeoutMs = timeout.has_value() ? timeout.value().count() : -1;
  if (mTimerFd != -1) {
    auto newVal = itimerspec {
        .it_interval = {.tv_sec = 0, .tv_nsec = 0},
        .it_value =
            {
                .tv_sec = timeout.has_value()
                              ? std::chrono::duration_cast<std::chrono::seconds, long>(timeout.value()).count()
                              : 0,
                .tv_nsec = 0,
            },
    };
    // LOG_INFO("timerfd_settime: timerfd: {}, it_interval={}.{}, it_value={}.{}", mTimerFd, newVal.it_interval.tv_sec,
    //          newVal.it_interval.tv_nsec, newVal.it_value.tv_sec, newVal.it_value.tv_nsec);
    RE(SysCall(::timerfd_settime, mTimerFd, 0, &newVal, nullptr));
    RE(mod(mTimerFd, Event::Readable(io::NOTIFY_KEY), PollMode::Oneshot));
  }

  // LOG_INFO("epoll_wait: epoll_fd={}, events.len={}, timeout={}", mEpollFd, events.len, timeoutMs);
  auto r = SysCall(::epoll_wait, mEpollFd, events.data.get(), events.len, timeoutMs);
  if (!r) {
    return make_unexpected(r.error());
  }
  // LOG_INFO("epoll_wait result: {}", r.value());
  events.len = r.value();

  auto buf = u64 {0};
  (SysCall(::read, mEventFd, &buf, sizeof(buf))); // ignore error
  // LOG_INFO("read eventfd: {} ({} bytes)", buf, sizeof(buf));
  RE(mod(mEventFd, Event::Readable(io::NOTIFY_KEY), PollMode::Oneshot));
  return {};
}

auto Poller::notify() -> StdResult<void>
{
  u64 num = 1ull;
  RE(SysCall(::write, mEventFd, &num, sizeof(num)));
  return {};
}

Poller::~Poller()
{
  // LOG_INFO("Poller destroyed: epoll_fd={}, event_fd={}, timer_fd={}", mEpollFd, mEventFd, mTimerFd);
  if (mEpollFd != -1) {
    ::close(mEpollFd);
  }
  if (mEventFd != -1) {
    ::close(mEventFd);
  }
  if (mTimerFd != -1) {
    ::close(mTimerFd);
  }
}

Poller::Poller(Poller&& other)
{
  mEpollFd = other.mEpollFd;
  mEventFd = other.mEventFd;
  mTimerFd = other.mTimerFd;
  other.mEpollFd = -1;
  other.mEventFd = -1;
  other.mTimerFd = -1;
}

auto Poller::operator=(Poller&& other) -> Poller&
{
  mEpollFd = other.mEpollFd;
  mEventFd = other.mEventFd;
  mTimerFd = other.mTimerFd;
  other.mEpollFd = -1;
  other.mEventFd = -1;
  other.mTimerFd = -1;
  return *this;
}
  #undef RE
} // namespace io::impl
#endif
