#include "io/sys/unix/Socket.hpp"
#ifdef UNIX_PLATFORM
  #include <fcntl.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>

namespace io {
auto LastError() -> std::error_code { return std::error_code(errno, std::system_category()); }
auto CreateSocket(SocketAddr const& addr, int ty) -> StdResult<Socket>
{
  auto family = addr.isIpv4() ? AF_INET : AF_INET6;
  auto rawSocket = socket(family, ty, 0);
  if (rawSocket != -1) {
    return Socket {rawSocket};
  } else {
    return make_unexpected(LastError());
  }
}
auto SocketAddrToSockAddr(SocketAddr const& addr, sockaddr_storage* storage, socklen_t* len) -> StdResult<void>
{
  if (addr.isIpv4()) {
    auto& ipv4 = addr.getIpv4();
    auto sockAddr = reinterpret_cast<sockaddr_in*>(storage);
    *len = sizeof(sockaddr_in);
    sockAddr->sin_family = AF_INET;
    sockAddr->sin_port = htons(ipv4.port());
    sockAddr->sin_addr.s_addr = htonl(*reinterpret_cast<u32 const*>(ipv4.address().addr.data()));
    return {};
  } else if (addr.isIpv6()) {
    UNIMPLEMENTED("ipv6");
    /*     auto& ipv6 = addr.ipv6();
        auto& sockAddr = reinterpret_cast<sockaddr_in6*>(storage);
        sockAddr->sin6_family = AF_INET6;
        sockAddr->sin6_port = htons(ipv6.port());
        sockAddr->sin6_flowinfo = 0;
        sockAddr->sin6_addr = ipv6.addr();
        sockAddr->sin6_scope_id = 0; */
    return {};
  } else {
    return make_unexpected(std::make_error_code(std::errc::address_family_not_supported));
  }
}
Socket::Socket(int rawSocket) : mFd(rawSocket) {}
Socket::Socket(Socket&& other) : mFd(other.mFd) { other.mFd = -1; }
auto Socket::raw() const -> int { return mFd; }
auto Socket::accept(sockaddr* storage, socklen_t len) -> StdResult<Socket>
{
  auto rawSocket = ::accept(mFd, storage, &len);
  if (rawSocket != -1) {
    return Socket {rawSocket};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::connect(SocketAddr const& addr) -> StdResult<void>
{
  sockaddr_storage storage;
  socklen_t len;
  if (auto r = SocketAddrToSockAddr(addr, &storage, &len); !r) {
    return make_unexpected(r.error());
  };
  auto res = ::connect(mFd, reinterpret_cast<sockaddr*>(&storage), len);
  if (res == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::close() -> StdResult<void>
{
  if (mFd != -1) {
    auto ret = ::close(mFd);
    mFd = -1;
    if (ret == 0) {
      return {};
    } else {
      return make_unexpected(LastError());
    }
  }
  return {};
}
auto Socket::recv(void* buf, size_t len, int flag) -> StdResult<size_t>
{
  auto result = ::recv(mFd, reinterpret_cast<char*>(buf), len, flag);
  if (result >= 0) {
    return result;
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::recvfrom(void* buf, size_t len, int flag) -> StdResult<size_t>
{
  sockaddr_storage storage;
  socklen_t storageLen = sizeof(storage);
  auto result =
      ::recvfrom(mFd, reinterpret_cast<char*>(buf), len, flag, reinterpret_cast<sockaddr*>(&storage), &storageLen);
  if (result >= 0) {
    return result;
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::read(void* buf, size_t len) -> StdResult<size_t> { return recv(buf, len, 0); }
auto Socket::setNonBlocking(bool enable) -> StdResult<void>
{
  auto flags = ::fcntl(mFd, F_GETFL, 0);
  if (flags == -1) {
    return make_unexpected(LastError());
  }
  if (enable) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  auto result = ::fcntl(mFd, F_SETFL, flags);
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::setLinger(bool enable, int timeout) -> StdResult<void>
{
  struct linger l;
  l.l_onoff = enable ? 1 : 0;
  l.l_linger = timeout;
  auto result = ::setsockopt(mFd, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l), sizeof(l));
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::linger() -> StdResult<Optional<std::chrono::seconds>>
{
  struct linger l;
  socklen_t len = sizeof(l);
  auto result = ::getsockopt(mFd, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l), &len);
  if (result == 0) {
    if (l.l_onoff) {
      return std::chrono::seconds {l.l_linger};
    } else {
      return {};
    }
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::setNoDelay(bool enable) -> StdResult<void>
{
  int enableInt = enable ? 1 : 0;
  auto result = ::setsockopt(mFd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&enableInt), sizeof(enableInt));
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::nodelay() -> StdResult<bool>
{
  int enable;
  socklen_t len = sizeof(enable);
  auto result = ::getsockopt(mFd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&enable), &len);
  if (result == 0) {
    return enable != 0;
  } else {
    return make_unexpected(LastError());
  }
}

} // namespace io
#endif