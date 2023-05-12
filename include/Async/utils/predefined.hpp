#pragma once
#include <cstdint>
#include <cassert>
#include <iostream>
#include <system_error>
#include <optional>

#define UNIMPLEMENTED(...)                                                                                             \
  do {                                                                                                                 \
    std::cerr << "Unimplemented: " << __FUNCTION__ << std::endl;                                                       \
    std::terminate();                                                                                                  \
  } while (0)

#include <system_error>
#include "expected.hpp"
template <typename... Args>
using Expected = tl::expected<Args...>;

template <typename T>
using StdResult = Expected<T, std::error_code>;
using tl::make_unexpected;
template <typename Fn, typename... Args>
auto SysCall(Fn&& fn, Args&&... args) -> StdResult<std::invoke_result_t<Fn, Args...>>
{
  auto result = std::invoke(fn, std::forward<Args>(args)...);
  if (result == -1) {
    return make_unexpected(std::error_code(errno, std::system_category()));
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