#include "Async/sys/unix/epoll.hpp"
#ifdef __linux__
  #include <Async/sys/Event.hpp>
  #include <sys/epoll.h>
  #include <sys/eventfd.h>
  #include <sys/fcntl.h>
  #include <sys/timerfd.h>
  #include <unistd.h>
namespace async::impl {
constexpr auto ReadFlags() -> uint32_t { return EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLPRI; }
constexpr auto WriteFlags() -> uint32_t { return EPOLLOUT | EPOLLHUP | EPOLLERR; }

auto Events::Iterator::operator*() const -> Event
{
  return async::Event {mPtr->data.u64, (mPtr->events & ReadFlags()) != 0, (mPtr->events & WriteFlags()) != 0};
}
auto Events::Iterator::operator->() const -> Event
{
  return async::Event {mPtr->data.u64, (mPtr->events & ReadFlags()) != 0, (mPtr->events & WriteFlags()) != 0};
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
  RE(poller.add(poller.mTimerFd, Event::None(async::NOTIFY_KEY), PollMode::Oneshot));
  RE(poller.add(poller.mEventFd, Event::Readable(async::NOTIFY_KEY), PollMode::Oneshot));
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

static timespec ns_to_timespec(std::optional<std::chrono::nanoseconds>& ns)
{
  using namespace std::chrono_literals;
  timespec ts;
  if (ns.has_value() && ns.value().count() > 0) {
    ts.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(ns.value()).count();
    ts.tv_nsec = (ns.value() - (ts.tv_sec * (1s))).count();
  } else {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
  }
  std::cout << "ts: " << ts.tv_sec << " " << ts.tv_nsec << "\n";
  return ts;
}

auto Poller::wait(Events& events, std::optional<std::chrono::nanoseconds> timeout) -> StdResult<void>
{

  if (mTimerFd != -1) {
    auto newVal = itimerspec {
        .it_interval = {.tv_sec = 0, .tv_nsec = 0},
        .it_value = ns_to_timespec(timeout),
    };
    RE(SysCall(::timerfd_settime, mTimerFd, 0, &newVal, nullptr));
    RE(mod(mTimerFd, Event::Readable(async::NOTIFY_KEY), PollMode::Oneshot));
  }

  int timeoutMs = -1;
  if (timeout) {
    if (mTimerFd == -1) {
      timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout.value()).count();
    } else {
      if (timeout->count() == 0) {
        timeoutMs = 0;
      }
    }
  }
  auto r = SysCall(::epoll_wait, mEpollFd, events.data.get(), Events::MAX_EVENTS, timeoutMs);
  if (!r) {
    return make_unexpected(r.error());
  }
  events.len = r.value();

  auto buf = uint64_t {0};
  (SysCall(::read, mEventFd, &buf, sizeof(buf))); // ignore error
  RE(mod(mEventFd, Event::Readable(async::NOTIFY_KEY), PollMode::Oneshot));
  return {};
}

auto Poller::notify() -> StdResult<void>
{
  uint64_t num = 1ull;
  RE(SysCall(::write, mEventFd, &num, sizeof(num)));
  return {};
}

Poller::~Poller()
{
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
  if (this != &other) {
    this->~Poller();
    new (this) Poller(std::move(other));
  }
  return *this;
}
  #undef RE
} // namespace async::impl
#endif
