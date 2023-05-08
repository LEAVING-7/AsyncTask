#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>
template <typename T>
class RingBuffer {
public:
  explicit RingBuffer(std::int64_t cap) : mCap(cap), mMask(cap - 1) { assert(cap && (!(cap & (cap - 1)))); }

  auto cap() const noexcept -> std::int64_t { return mCap; }
  auto store(int64_t i, T&& x) noexcept { mBuf[i & mMask] = std::move(x); }
  auto load(int64_t i) const noexcept -> T { return mBuf[i & mMask]; }
  auto resize(int64_t b, int64_t t) const -> RingBuffer<T>*
  {
    auto ptr = new RingBuffer {2 * mCap};
    for (int64_t i = t; i != b; ++i) {
      ptr->store(i, std::move(load(i)));
    }
    return ptr;
  }

private:
  int64_t mCap;
  int64_t mMask;

  std::unique_ptr<T[]> mBuf = std::make_unique_for_overwrite<T[]>(mCap);
};

constexpr std::size_t hardware_destructive_interference_size = 64;

template <typename T>
class ConcurrentQueue {
  using std::memory_order::acq_rel;
  using std::memory_order::acquire;
  using std::memory_order::relaxed;
  using std::memory_order::release;
  using std::memory_order::seq_cst;

public:
  explicit ConcurrentQueue(int64_t cap = 1024) : mTop(0), mBottom(0), mBuffer(new RingBuffer<T> {cap})
  {
    mGarbage.reserve(32);
  }

  ConcurrentQueue(ConcurrentQueue const& other) = delete;
  ConcurrentQueue& operator=(ConcurrentQueue const& other) = delete;

  auto size() const noexcept -> size_t
  {
    auto b = mBottom.load(relaxed);
    auto t = mTop.load(relaxed);
    return static_cast<size_t>(b >= t ? b - t : 0);
  }
  auto empty() const noexcept -> bool { return size() == 0; }
  auto cap() const noexcept -> int64_t { return mBuffer.load(relaxed)->cap(); };

  template <typename... Args>
  void emplace(Args&&... args)
  {
    auto obj = T {std::forward<Args>(args)...};
    auto b = mBottom.load(relaxed);
    auto t = mTop.load(acquire);
    auto buf = mBuffer.load(relaxed);

    if (b - t > buf->cap() - 1) {
      mGarbage.emplace_back(std::exchange(buf, buf->resize(b, t)));
      mBuffer.store(buf, relaxed);
    }

    buf->store(b, std::move(obj));

    std::atomic_thread_fence(release);
    mBottom.store(b + 1, relaxed);
  }

  auto pop() noexcept -> std::optional<T>
  {
    auto b = mBottom.load(relaxed) - 1;
    auto buf = mBuffer.load(relaxed);

    mBottom.store(b, relaxed);

    std::atomic_thread_fence(seq_cst);

    auto t = mTop.load(relaxed);

    if (t <= b) {
      if (t == b) {
        if (!mTop.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
          mBottom.store(b + 1, relaxed);
          return std::nullopt;
        }
        mBottom.store(b + 1, relaxed);
      }
      return buf->load(b);
    } else {
      mBottom.store(b + 1, relaxed);
      return std::nullopt;
    }
  }
  auto steal() noexcept -> std::optional<T>
  {
    auto t = mTop.load(acquire);
    std::atomic_thread_fence(seq_cst);
    auto b = mBottom.load(acquire);

    if (t < b) {
      auto x = mBuffer.load(seq_cst)->load(t);
      if (!mTop.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
        return std::nullopt;
      }
      return x;
    } else {
      return std::nullopt;
    }
  }
  ~ConcurrentQueue() noexcept { delete mBuffer.load(); };

private:
  alignas(hardware_destructive_interference_size) std::atomic_int64_t mTop;
  alignas(hardware_destructive_interference_size) std::atomic_int64_t mBottom;
  alignas(hardware_destructive_interference_size) std::atomic<RingBuffer<T>*> mBuffer;

  std::vector<std::unique_ptr<RingBuffer<T>>> mGarbage;
};

template <typename T>
auto Steal(ConcurrentQueue<T>& src, ConcurrentQueue<T>& dst) -> bool
{
  auto count = (src.size() + 1) / 2;
  if (count > 0) {
    if (dst.cap()) {
      count = std::min(count, dst.cap() - dst.size());
    }
    for (auto i = 0; i < count; ++i) {
      if (auto x = src.steal(); x) {
        dst.emplace(std::move(x.value()));
      } else {
        return false;
      }
    }
  }
  return true;
}