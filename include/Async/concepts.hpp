#pragma once
#include <concepts>
#include <coroutine>
namespace async {
class MultiThreadExecutor;
class InlineExecutor;
template <typename T>
concept ExecutorCpt = std::is_same_v<MultiThreadExecutor, T> || std::is_same_v<InlineExecutor, T>;
} // namespace async