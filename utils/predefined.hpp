#pragma once
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

#include <cassert>
#include <iostream>
#include <system_error>
#include <optional>

#define UNIMPLEMENTED(...)                                                                                             \
  do {                                                                                                                 \
    std::cerr << "Unimplemented: " << __FUNCTION__ << std::endl;                                                       \
    std::terminate();                                                                                                  \
  } while (0)

#include "expected.hpp"
template <typename... Args>
using Expected = tl::expected<Args...>;

template <typename T>
using StdResult = Expected<T, std::error_code>;

using tl::make_unexpected;
template <typename T>
using Optional = std::optional<T>;

#include <system_error>
template <typename Fn, typename... Args>
auto SysCall(Fn fn, Args... args) -> StdResult<std::invoke_result_t<Fn, Args...>>
{
  auto result = std::invoke(fn, args...);
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