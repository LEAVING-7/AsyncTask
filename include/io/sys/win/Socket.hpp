#pragma once
#include <cassert>
#include <mutex>
#include <winsock2.h>

#include "io/net/SocketAddr.hpp"
#include "predefined.hpp"

namespace io {
using sa_family = ADDRESS_FAMILY;
using addrinfo = ADDRINFOA;
using sockaddr = SOCKADDR;
using sockaddr_storage = SOCKADDR_STORAGE_LH;

static std::once_flag gWinSockInitFlag;

auto Init() -> void;
auto CleanUp() -> void;
auto LastError() -> std::error_code;

// manual close
class Socket {
public:
  Socket(SOCKET handle);
  Socket(Socket&& other);

  auto accept(sockaddr* storage, int len) -> StdResult<Socket>;
  auto connect(SocketAddr const& addr) -> StdResult<void>;
  auto recv(void* buf, size_t len, int flag) -> StdResult<size_t>;
  auto recvfrom(void* buf, size_t len, int flag) -> StdResult<size_t>;
  auto read(void* buf, size_t len) -> StdResult<size_t>;

  auto setNonBlocking(bool nonBlocking) -> StdResult<void>;

  auto setLinger(bool enable, int timeout) -> StdResult<void>;
  auto linger() -> StdResult<Optional<std::chrono::seconds>>;
  auto setNoDelay(bool enable) -> StdResult<void>;
  auto nodelay() -> StdResult<bool>;

  auto close() -> StdResult<void>;
  auto raw() const -> SOCKET;

private:
  SOCKET mHandle = INVALID_SOCKET;
};

auto CreateSocket(SocketAddr const& addr, int ty) -> StdResult<Socket>;
auto SocketAddrToSockAddr(SocketAddr const& addr, sockaddr_storage* storage, int* len) -> StdResult<void>;
} // namespace io