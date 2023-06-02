#pragma once
#include <cassert>
#include <cstdint>
#include <optional>
#include <system_error>

#include "expected.hpp"
#include <system_error>
template <typename... Args>
using Expected = tl::expected<Args...>;

template <typename T = void>
using StdResult = Expected<T, std::errc>;
using tl::make_unexpected;
template <typename Fn, typename... Args>
auto SysCall(Fn&& fn, Args&&... args) -> StdResult<std::invoke_result_t<Fn, Args...>>
{
  auto result = std::invoke(fn, std::forward<Args>(args)...);
  if (result == -1) {
    return make_unexpected(std::errc(errno));
  } else {
    return result;
  }
}

#include <variant>
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;