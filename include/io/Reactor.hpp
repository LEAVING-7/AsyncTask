#pragma once
#include "ConcurrentQueue.hpp"
#include "Slab.hpp"
#include "platform.hpp"
#include "sys/Event.hpp"
#include <chrono>
#include <coroutine>
#include <map>
#include <span>

namespace io {
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

struct Direction {
  // size_t tick;
  std::coroutine_handle<> handle {nullptr};

  auto takeHandle() -> std::coroutine_handle<> { return std::exchange(handle, nullptr); }
  auto isEmpty() const -> bool { return handle == nullptr; }
};

struct Source {
  struct State {
    Direction read;
    Direction write;
  };

  Source(int fd, size_t key) : fd(fd), key(key) {}
  int const fd;
  size_t key;

  std::mutex stateLock;
  State state;
  auto setReadable(std::coroutine_handle<> handle) -> bool
  {
    auto lk = std::unique_lock {stateLock};
    if (state.read.isEmpty()) {
      state.read.handle = handle;
      return true;
    }
    return false;
  }
  auto setWritable(std::coroutine_handle<> handle) -> bool
  {
    auto lk = std::unique_lock {stateLock};
    if (state.write.isEmpty()) {
      state.write.handle = handle;
      return true;
    }
    return false;
  }
  auto getEvent() -> Event
  {
    auto lk = std::unique_lock {stateLock};
    auto event = Event::None(key);
    if (!state.read.isEmpty()) {
      event.readable = true;
    }
    if (!state.write.isEmpty()) {
      event.writable = true;
    }
    return event;
  }
};

struct TimerOp {
  struct Insert {
    size_t key;
    TimePoint when;
    std::coroutine_handle<> handle;
  };
  struct Remove {
    size_t key;
    TimePoint when;
  };
  std::variant<Insert, Remove> op;
};

class Reactor;

struct ReactorLock {
  Reactor& reactor;
  std::unique_lock<std::mutex> eventLock; // eventLock must be held

  template <typename ExecutorType>
  auto react(std::optional<TimePoint::duration> timeout, ExecutorType& e) -> StdResult<void>;
};

class Reactor {
  using TimersType = std::map<std::pair<TimePoint, size_t>, std::coroutine_handle<>>;

public:
  Reactor() : mPoller(), mTicker(0), mSources(), mEvents(), mTimers(), mTimerOps() {}
  ~Reactor() {}
  auto ticker() -> size_t { return mTicker.load(); }
  auto insertIo(int fd) -> StdResult<std::shared_ptr<Source>>
  {
    auto sourceLk = std::unique_lock {mSourceLock};
    auto source = std::make_shared<Source>(fd, 0);
    auto key = mSources.insert(source);
    source->key = key;
    sourceLk.unlock();

    if (auto r = mPoller.add(fd, Event::None(key)); !r) {
      auto lk = std::unique_lock {mSourceLock};
      auto e = mSources.tryRemove(key);
      assert(e);
      return make_unexpected(r.error());
    }
    return source;
  }
  auto removeIo(Source const& source) -> StdResult<void>
  {
    auto lk = std::unique_lock {mSourceLock};
    auto e = mSources.tryRemove(source.key);
    assert(e && "remove invalid key");
    return mPoller.del(source.fd);
  }
  auto updateIo(Source const& source) -> StdResult<void>
  {
    auto lk = std::unique_lock {mSourceLock};
    auto e = mSources.get(source.key);
    assert(e);
    auto event = e->get()->getEvent();
    return mPoller.mod(source.fd, event);
  }
  auto sleep(TimePoint::duration duration)
  {
    struct SleepAwaiter {
      io::Reactor* reactor;
      TimePoint when;
      size_t id = std::numeric_limits<size_t>::max();
      SleepAwaiter(io::Reactor* reactor, TimePoint when) : reactor(reactor), when(when) {}
      auto await_ready() const -> bool { return false; }
      auto await_suspend(std::coroutine_handle<> handle) -> void { id = reactor->insertTimer(when, handle); }
      auto await_resume() const -> void {}
    };
    return SleepAwaiter {this, TimePoint::clock::now() + duration};
  }
  auto insertTimer(TimePoint when, std::coroutine_handle<> handle) -> size_t
  {
    static auto ID_GENERATOR = std::atomic_size_t {0};
    auto id = ID_GENERATOR.fetch_add(1, std::memory_order_relaxed);
    mTimerOps.emplace(TimerOp::Insert {id, when, handle});
    notify();
    return id;
  }
  auto removeTimer(TimePoint when, size_t id) -> void { mTimerOps.emplace(TimerOp::Remove {id, when}); }
  auto notify() -> void
  {
    if (auto r = mPoller.notify(); !r) {
      assert("poller notify failed" && false);
    };
  }
  auto processTimers(std::vector<std::coroutine_handle<>>& handles) -> std::optional<TimePoint::duration>
  {
    using namespace std::chrono_literals;
    auto lk = std::unique_lock {mTimerLock};
    processTimeOps(mTimers);
    auto pending = std::vector<std::pair<TimePoint, size_t>> {};
    auto ready = std::vector<std::coroutine_handle<>> {};
    auto now = TimePoint::clock::now() + 1ns;
    // TODO: split timers into ready and pending
    for (auto const& entry : mTimers) {
      if (entry.first.first <= now) {
        ready.push_back(entry.second);
        mTimers.erase(entry.first);
      } else {
        pending.push_back(entry.first);
      }
    }

    auto duration = std::optional<TimePoint::duration> {std::nullopt};
    if (ready.empty()) {
      auto it = std::min_element(pending.begin(), pending.end(),
                                 [](auto const& a, auto const& b) { return a.first < b.first; });
      if (it != pending.end()) {
        duration = (it->first - now).count() < 0 ? 0ns : it->first - now;
      } else {
        duration = 0ns;
      }
    }
    lk.unlock();
    for (auto const handle : ready) {
      handles.push_back(handle);
    }
    return duration;
  }

  auto processTimeOps(TimersType& mTimers) -> void
  {
    while (true) {
      if (auto r = mTimerOps.pop(); r) {
        auto fn = overloaded {
            [&](TimerOp::Insert const& op) {
              mTimers.insert({{op.when, op.key}, op.handle});
            },
            [&](TimerOp::Remove const& op) {
              mTimers.erase({op.when, op.key});
            },
        };
        std::visit(fn, r.value().op);
      } else {
        break;
      }
    }
  }

  auto lock() -> ReactorLock
  {
    auto eventLock = std::unique_lock {mEventLock};
    return ReactorLock {*this, std::move(eventLock)};
  }

  auto tryLock() -> std::optional<ReactorLock>
  {
    auto eventLock = std::unique_lock {mEventLock, std::try_to_lock};
    if (!eventLock.owns_lock()) {
      return std::nullopt;
    }
    return ReactorLock {*this, std::move(eventLock)};
  }

  friend struct ReactorLock;

private:
  io::Poller mPoller;
  std::atomic_size_t mTicker;

  std::mutex mSourceLock;
  Slab<std::shared_ptr<Source>> mSources;

  std::mutex mEventLock;
  std::vector<Event> mEvents;

  std::mutex mTimerLock;
  TimersType mTimers;

  ConcurrentQueue<TimerOp> mTimerOps;
};

template <typename ExecutorType>
inline auto ReactorLock::react(std::optional<TimePoint::duration> timeout, ExecutorType& e) -> StdResult<void>
{
  using namespace std::chrono_literals;
  auto handles = std::vector<std::coroutine_handle<>> {};

  auto nextTimer = reactor.processTimers(handles);
  auto waitTimeout = std::optional<TimePoint::duration> {std::nullopt};
  if (timeout && !nextTimer) {
    waitTimeout.emplace(timeout.value());
  } else if (timeout && nextTimer) {
    waitTimeout.emplace(std::min(timeout.value(), nextTimer.value()));
  } else if (!timeout && nextTimer) {
    waitTimeout.emplace(nextTimer.value());
  }

  auto tick = reactor.mTicker.fetch_add(1) + 1;
  reactor.mEvents.clear();
  if (auto r = reactor.mPoller.wait(reactor.mEvents, waitTimeout); r) {
    if (r.value() == 0) {
      if (*waitTimeout != 0s) {
        reactor.processTimers(handles);
      }
    } else {
      auto lk = std::unique_lock {reactor.mSourceLock};
      for (auto const& ev : reactor.mEvents) {
        if (auto ptr = reactor.mSources.get(ev.key); ptr) {
          auto stateLk = std::unique_lock {ptr->get()->stateLock};
          auto& state = ptr->get()->state;
          if (ev.writable) {
            // state.write.tick = tick;
            handles.push_back(state.write.takeHandle());
          } else if (ev.readable) {
            // state.read.tick = tick;
            handles.push_back(state.read.takeHandle());
          }
        }
      }
    }
  } else if (r.error() == std::errc::interrupted) {
    for (auto handle : handles) {
      e.execute(handle);
    }
    return make_unexpected(r.error());
  } else {
    for (auto handle : handles) {
      e.execute(handle);
    }
    return make_unexpected(r.error());
  }
  for (auto handle : handles) {
    e.execute(handle);
  }
  return {};
}
} // namespace io