#pragma once
#include "ConcurrentQueue.hpp"
#include "Reactor.hpp"
#include "Slab.hpp"
#include "async/Task.hpp"
#include "log.hpp"
#include <deque>
#include <future>
#include <random>
#include <shared_mutex>

namespace io {
class State {
public:
  friend class Runner;

  State() : mGlobal(), mLocalQueuesMt(), mLocals(), mActiveMt(), mActives(), mWaitingMt(), mWaiting()
  {
    mActives.reserve(1024);
  }

  auto isEmpty() -> bool
  {
    auto globEmpty = mGlobal.empty();
    auto lk = std::shared_lock(mActiveMt);
    auto actEmpty = mActives.isEmpty();
    return actEmpty && globEmpty;
  }

  auto waitEmpty() -> void
  {
    auto lk = std::shared_lock(mActiveMt);
    mActiveCv.wait(lk, [this]() { return mActives.isEmpty(); });
  }

  auto spawn(Task<> in) -> void
  {
    mActiveMt.lock();
    auto key = mActives.insert(in.handle());
    mActiveMt.unlock();
    auto cleanUpFn = [this, key]() {
      mActiveMt.lock();
      auto r = mActives.tryRemove(key);
      assert(r);
      if (mActives.isEmpty()) {
        mActiveCv.notify_one();
      }
      mActiveMt.unlock();
    };
    auto cleanUpCo =
        [this](Task<> task) -> DetachTask<void> { co_return co_await task; }(std::move(in)).afterDestroy(cleanUpFn);
    std::coroutine_handle<> handle = cleanUpCo.handle;
    execute(handle);
  }

  template <typename T>
  [[nodiscard]] auto blockOn(Task<T> in, io::Reactor& reactor) -> std::future<T>
  {
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();
    if constexpr (std::is_void_v<T>) {
      auto cleanFn = [promise, &reactor]() {
        promise->set_value();
        LOG_INFO("notify reactor");
        reactor.notify();
      };
      auto cleanUpCo = [](Task<T> task)
          -> DetachTask<T> { co_return co_await task; }(std::move(in)).afterDestroy(std::move(cleanFn));
      execute(cleanUpCo.handle);
    } else {
      auto cleanFn = [promise, &reactor](auto&& value) {
        promise->set_value(std::forward<T>(value));
        LOG_INFO("notify reactor");
        reactor.notify();
      };
      auto cleanUpCo = [](Task<T> task)
          -> DetachTask<T> { co_return co_await task; }(std::move(in)).afterDestroy(std::move(cleanFn));
      execute(cleanUpCo.handle);
    }
    return future;
  }

  auto execute(std::coroutine_handle<> handle) -> void
  {
    LOG_INFO("execute handle : {}", handle.address());
    mGlobal.emplace(handle);
    LOG_INFO("global size : {}", mGlobal.size());
    mWaiting.notify_one();
  }

private:
  ConcurrentQueue<std::coroutine_handle<>> mGlobal;
  std::shared_mutex mLocalQueuesMt;
  std::vector<std::shared_ptr<ConcurrentQueue<std::coroutine_handle<>>>> mLocals;

  // std::atomic_bool mNotified;

  std::shared_mutex mActiveMt;
  Slab<std::coroutine_handle<>> mActives;
  std::condition_variable_any mActiveCv;

  std::mutex mWaitingMt;
  std::condition_variable_any mWaiting;
};

class Runner {
public:
  Runner(State& state) : mState(state), mLocal(), mTicks(0)
  {
    mLocal = std::make_shared<ConcurrentQueue<std::coroutine_handle<>>>();
    mState.mLocalQueuesMt.lock();
    mState.mLocals.push_back(mLocal);
    mState.mLocalQueuesMt.unlock();

    mThread = std::jthread([this](std::stop_token tok) { run(tok); });
  }
  ~Runner() { mThread.get_stop_source().request_stop(); }

  auto acquire() -> std::coroutine_handle<>
  {
    if (auto h = mLocal->pop(); h) {
      LOG_CRITICAL("acqure from local: {}", h->address());
      return h.value();
    } else if (auto h = mState.mGlobal.pop(); h) {
      Steal(mState.mGlobal, *mLocal);
      LOG_INFO("steal from global : {}", h->address());
      LOG_INFO("local size : {}", mLocal->size());
      return h.value();
    } else {
      auto lk = std::unique_lock(mState.mLocalQueuesMt);

      auto n = mState.mLocals.size();
      static auto rd = std::random_device();
      auto idst = std::uniform_int_distribution<size_t>(0, n);
      auto start = idst(rd);

      for (size_t i = 0; i < n * 2; ++i) {
        auto idx = (start + i) % n;
        auto& q = mState.mLocals[idx];
        if (q == mLocal) {
          continue;
        }
        Steal(*q, *mLocal);
        if (auto h = mLocal->pop(); h) {
          LOG_INFO("steal from other local : {}", h->address());
          return h.value();
        }
      }
    }
    return {nullptr};
  }

  auto run(std::stop_token tok) -> void
  {
    while (!tok.stop_requested()) {
      auto lk = std::unique_lock(mState.mWaitingMt);
      mState.mWaiting.wait(lk, tok, [this]() { return !mState.mGlobal.empty(); });
      LOG_INFO("wake up at: {}, global size: {}", std::this_thread::get_id(), mState.mGlobal.size());
      if (auto h = acquire(); h) {
        LOG_INFO("resume handle : {}", h.address());
        h.resume();
      } else {
        continue;
      }
      // resume each handle in local queue
      while (auto h = mLocal->pop()) {
        if (h.has_value()) {
          LOG_INFO("resume handle : {}", h.value().address());
          h.value().resume();
        } else {
          break;
        }
      }
      auto ticks = mTicks.fetch_add(1);
      if (ticks % 64 == 0) {
        Steal(mState.mGlobal, *mLocal);
      }
    }
  }

private:
  State& mState;
  std::shared_ptr<ConcurrentQueue<std::coroutine_handle<>>> mLocal;
  std::atomic_size_t mTicks;
  std::jthread mThread;
};

class Executor {
  friend Runner;

public:
  Executor() : mState()
  {
    for (int i = 0; i < 2; ++i) {
      auto runner = std::make_shared<Runner>(mState);
      mRunners.push_back(runner);
    }
  }
  ~Executor() { mState.waitEmpty(); }

  auto isEmpty() -> bool { return mState.isEmpty(); }
  auto spawn(Task<> task) -> void { mState.spawn(std::move(task)); }
  auto execute(std::coroutine_handle<> handle) -> void { return mState.execute(handle); }
  auto waitEmpty() -> void { mState.waitEmpty(); }
  template <typename T>
  [[nodiscard]] auto blockOn(Task<T> task, io::Reactor& reactor) -> std::future<T>
  {
    return mState.blockOn(std::move(task), reactor);
  }

private:
  State mState;
  std::vector<std::shared_ptr<Runner>> mRunners;
};

auto constexpr DEFAULT_MAX_THREAD = 500;
auto constexpr MIN_MAX_THREAD = 1;
auto constexpr MAX_MAX_THREAD = 10000;

class BlockingExecutor {
public:
  auto schedule(std::coroutine_handle<> handle) -> void
  {
    auto lk = std::unique_lock(mMt);
    mQueue.push_back(std::move(handle));
    mCv.notify_one();
    growPool();
  }

private:
  auto threadLoop() -> void
  {
    using namespace std::chrono_literals;
    auto lk = std::unique_lock(mMt);
    while (true) {
      mIdleCount -= 1;
      while (!mQueue.empty()) {
        growPool();
        auto task = std::move(mQueue.front());
        mQueue.pop_front();
        lk.unlock();
        task.resume();
        lk.lock();
      }
      mIdleCount += 1;
      auto r = mCv.wait_for(lk, 500ms);
      if (r == std::cv_status::timeout && mQueue.empty()) {
        mIdleCount -= 1;
        mThreadCount -= 1;
        break;
      }
    }
  }

  auto growPool() -> void
  {
    assert(!mMt.try_lock());
    while (mQueue.size() > mIdleCount * 5 && mThreadCount < mThreadLimits) {
      mThreadCount += 1;
      mIdleCount += 1;
      mCv.notify_all();
      std::thread([this]() { threadLoop(); }).detach();
    }
  }

  std::mutex mMt;
  std::condition_variable mCv;

  size_t mIdleCount;
  size_t mThreadCount;
  std::deque<std::coroutine_handle<>> mQueue;
  size_t mThreadLimits;
};
} // namespace io