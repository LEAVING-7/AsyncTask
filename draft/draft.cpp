#include <mutex>

// #include "io/Slab.hpp"
// #include "io/net/SocketAddr.hpp"
// #include "io/sys/common/tcp.hpp"
#include <iostream>

#include <inttypes.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <iostream>

#define handle_error(msg)                                                                                              \
  do {                                                                                                                 \
    perror(msg);                                                                                                       \
    exit(EXIT_FAILURE);                                                                                                \
  } while (0)

int main(int argc, char* argv[])
{
  struct itimerspec ts;
  struct timespec start, now;
  int maxExp, fd, secs, nanosecs;
  std::uint64_t numExp, totalExp;
  ssize_t s;

  fd = timerfd_create(CLOCK_REALTIME, 0);
  if (fd == -1) {
    handle_error("timerfd_create");
  }
  ts = itimerspec {
      .it_interval =
          {
              .tv_sec = 4, // 4s
              .tv_nsec = 0,
          },
      .it_value =
          {
              .tv_sec = 4, // 4s
              .tv_nsec = 0,
          },
  };
  auto r = timerfd_settime(fd, 0, &ts, NULL);
  if (r == -1) {
    handle_error("timerfd_settime");
  }
  r = clock_gettime(fd, &start);
  if (r == -1) {
    handle_error("clock_gettime");
  }
  std::cout << start.tv_sec << ' ' << start.tv_nsec << std::endl;
}