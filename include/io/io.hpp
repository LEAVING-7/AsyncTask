#pragma once
#include "platform.hpp"

#if defined(WIN_PLATFORM)
#include "io/sys/win/io.hpp"
#elif defined(UNIX_PLATFORM)
// #include "io/unix/io.hpp"
#endif
