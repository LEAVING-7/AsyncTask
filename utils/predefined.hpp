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
#define UNIMPLEMENTED(...)                                                                                             \
  do {                                                                                                                 \
    std::cerr << "Unimplemented: " << __FUNCTION__ << std::endl;                                                       \
    std::terminate();                                                                                                  \
  } while (0)

#include "expected.hpp"
template <typename... Args>
using Expected = tl::expected<Args...>;

#include <system_error>
template <typename T>
using StdResult = Expected<T, std::error_code>;

using tl::make_unexpected;
#include <optional>
template <typename T>
using Optional = std::optional<T>;