#pragma once
#include "Async/utils/predefined.hpp"
#include <variant>
#include <vector>

template <typename T>
struct Entry {
  std::variant<T, size_t> data;

  auto setOccupied(T&& value) { data = std::move(value); }
  auto setVacant(size_t next) { data = next; }
  auto getData() -> T& { return std::get<T>(data); }
  auto getData() const -> T const& { return std::get<T>(data); }
  auto getNext() const -> size_t { return std::get<size_t>(data); }
  auto isVacant() const -> bool { return std::holds_alternative<size_t>(data); }
  auto isOccupied() const -> bool { return std::holds_alternative<T>(data); }
};

template <typename T>
class Slab;

template <typename T>
struct VacantEntry {
  Slab<T>& slab;
  size_t key;

  auto insert(T&& value) -> T&
  {
    slab.insertAt(key, std::move(value));
    return slab[key];
  }
};

template <typename T>
class Slab {
public:
  Slab() : mEntries(), mLen(0), mNext(0) {};
  auto len() const { return mLen; }
  auto isEmpty() const { return mLen == 0; }
  auto capacity() const { return mEntries.capacity(); }
  auto clear()
  {
    mEntries.clear();
    mLen = 0;
    mNext = 0;
  }
  auto reserve(size_t size) { mEntries.reserve(size); }
  auto shrinkToFit()
  {
    auto lenBefore = mEntries.size();
    while (mEntries.back().isVacant()) {
      mEntries.pop_back();
    }

    if (mEntries.size() != lenBefore) {
    }
    mEntries.shrink_to_fit();
  }
  auto recreateVacantList()
  {
    mNext = mEntries.size();

    auto remaining = mEntries.size() - mLen;
    if (remaining == 0) {
      return;
    }

    for (auto i = mEntries.size() - 1; i > 0; --i) {
      if (mEntries[i].isVacant()) {
        mEntries[i].setVacant(mNext);
        mNext = i;
        remaining -= 1;
        if (remaining == 0) {
          break;
        }
      }
    }
  }
  auto getEntry(size_t index) -> std::optional<std::reference_wrapper<Entry<T>>>
  {
    if (index >= mEntries.size()) {
      return std::nullopt;
    }
    return mEntries[index];
  }
  auto get(size_t index) -> T*
  {
    assert(index < mEntries.size());
    if (mEntries[index].isOccupied()) {
      return &mEntries[index].getData();
    } else {
      return nullptr;
    }
  }
  auto get(size_t index) const -> T const*
  {
    assert(index < mEntries.size());
    if (mEntries[index].isOccupied()) {
      return &mEntries[index].getData();
    } else {
      return nullptr;
    }
  }
  auto insertAt(size_t key, T&& data)
  {
    mLen += 1;
    if (key == mEntries.size()) {
      mEntries.emplace_back(Entry<T> {std::move(data)});
      mNext = key + 1;
    } else {
      if (auto& entry = mEntries[key]; entry.isVacant()) {
        mNext = entry.getNext();
        entry.setOccupied(std::move(data));
      } else {
        assert(0);
      }
    }
  }
  auto insert(T&& data) -> size_t
  {
    auto key = mNext;
    insertAt(key, std::move(data));
    return key;
  }
  auto insert(T const& data) -> size_t { return insert(T {data}); }
  auto tryRemove(size_t key) -> std::optional<T>
  {
    if (auto entry = getEntry(key); entry.has_value()) {
      if (entry->get().isOccupied()) {
        mLen -= 1;
        auto value = std::move(entry->get().getData());
        entry->get().setVacant(mNext);
        mNext = key;
        return std::move(value);
      }
    }
    return std::nullopt;
  }
  auto contains(size_t key) -> bool
  {
    if (auto entry = getEntry(key); entry.has_value()) {
      return entry->get().isOccupied();
    }
    return false;
  }
  auto vacantEntry() -> VacantEntry<T> { return VacantEntry<T> {*this, mNext}; }
  auto operator[](size_t key) -> T& { return *get(key); }

private:
  std::vector<Entry<T>> mEntries;
  size_t mLen;
  size_t mNext;
};