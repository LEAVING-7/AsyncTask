#pragma once
#include "platform.hpp"
#ifdef UNIX_PLATFORM
  #include "io/net/SocketAddr.hpp"
  #include <arpa/inet.h>
  #include <chrono>
  #include <sys/types.h>

namespace io {
using sa_family = sa_family_t;
using addrinfo = struct addrinfo;
using sockaddr = sockaddr;
using sockaddr_storage = sockaddr_storage;
using socklen_t = socklen_t;

inline auto Init() -> void {};
auto LastError() -> std::error_code;

class Socket {
public:
  Socket() = default;
  Socket(int handle);
  Socket(Socket&& other);
  Socket& operator=(Socket&& other);

  auto accept(sockaddr* storage, socklen_t len) -> StdResult<Socket>;
  auto connect(SocketAddr const& addr) -> StdResult<void>;
  auto recv(void* buf, size_t len, int flag) -> StdResult<size_t>;
  auto recvfrom(void* buf, size_t len, int flag) -> StdResult<size_t>;
  auto read(void* buf, size_t len) -> StdResult<size_t>;
  auto send(void const* buf, size_t len, int flag) -> StdResult<size_t>;
  auto sendto(void const* buf, size_t len, SocketAddr const& addr, int flag) -> StdResult<size_t>;
  auto setNonBlocking(bool nonBlocking) -> StdResult<void>;

  auto setLinger(bool enable, int timeout) -> StdResult<void>;
  auto linger() const -> StdResult<Optional<std::chrono::seconds>>;
  auto setNoDelay(bool enable) -> StdResult<void>;
  auto nodelay() const -> StdResult<bool>;

  auto close() -> StdResult<void>;
  auto raw() const -> int;
  auto valid() const -> bool;

private:
  int mFd = -1;
};

auto CreateSocket(SocketAddr const& addr, int ty) -> StdResult<Socket>;
auto SocketAddrToSockAddr(SocketAddr const& addr, sockaddr_storage* storage, socklen_t* len) -> StdResult<void>;
} // namespace io

#endif
