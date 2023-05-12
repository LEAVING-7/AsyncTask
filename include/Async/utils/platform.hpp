#pragma once

#if defined(_WIN32)
#define WIN_PLATFORM
#elif defined(__linux__)
#define UNIX_PLATFORM
#endif
