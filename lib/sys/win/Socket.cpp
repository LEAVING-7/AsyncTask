#include "io/sys/win/Socket.hpp"

namespace io {

auto Init() -> void
{
  std::call_once(gWinSockInitFlag, []() {
    WSADATA wsaData;
    auto result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    assert(result == 0);
  });
}
auto CleanUp() -> void { WSACleanup(); }
auto LastError() -> std::error_code { return std::error_code {WSAGetLastError(), std::system_category()}; }

auto CreateSocket(SocketAddr const& addr, int ty) -> StdResult<Socket>
{
  auto family = addr.isIpv4() ? AF_INET : AF_INET6;
  auto rawSocket = WSASocketW(family, ty, 0, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (rawSocket != INVALID_SOCKET) {
    return Socket {rawSocket};
  } else {
    auto wsaError = WSAGetLastError();
    if (wsaError != WSAEPROTOTYPE && wsaError != WSAEINVAL) {
      return make_unexpected(LastError());
    }

    auto rawSocket = WSASocketW(family, ty, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);

    if (rawSocket != INVALID_SOCKET) {
      return Socket {rawSocket};
    } else {
      return make_unexpected(LastError());
    }
  }
}

auto SocketAddrToSockAddr(SocketAddr const& addr, sockaddr_storage* storage, int* len) -> StdResult<void>
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

#define RE(expr)                                                                                                       \
  if (auto res = (expr); !res.has_value()) {                                                                           \
    return res;                                                                                                        \
  }
Socket::Socket(SOCKET handle) : mHandle(handle) {}
Socket::Socket(Socket&& other) : mHandle(other.mHandle) { other.mHandle = INVALID_SOCKET; }

auto Socket::raw() const -> SOCKET { return mHandle; }

auto Socket::accept(sockaddr* storage, int len) -> StdResult<Socket>
{
  auto rawSocket = ::accept(mHandle, storage, &len);
  if (rawSocket != INVALID_SOCKET) {
    return Socket {rawSocket};
  } else {
    return make_unexpected(LastError());
  }
}

auto Socket::connect(SocketAddr const& addr) -> StdResult<void>
{
  sockaddr_storage storage;
  int len;
  RE(SocketAddrToSockAddr(addr, &storage, &len));
  auto res = ::connect(mHandle, reinterpret_cast<sockaddr*>(&storage), len);
  if (res == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}

auto Socket::close() -> StdResult<void>
{
  if (mHandle != INVALID_SOCKET) {
    auto ret = ::closesocket(mHandle);
    mHandle = INVALID_SOCKET;
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
  auto result = ::recv(mHandle, reinterpret_cast<char*>(buf), len, flag);
  if (result >= 0) {
    return result;
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::recvfrom(void* buf, size_t len, int flag) -> StdResult<size_t>
{
  sockaddr_storage storage;
  int storageLen = sizeof(storage);
  auto result =
      ::recvfrom(mHandle, reinterpret_cast<char*>(buf), len, flag, reinterpret_cast<sockaddr*>(&storage), &storageLen);
  if (result >= 0) {
    return result;
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::read(void* buf, size_t len) -> StdResult<size_t> { return recv(buf, len, 0); }
auto Socket::setNonBlocking(bool nonBlocking) -> StdResult<void>
{
  auto result = ::ioctlsocket(mHandle, FIONBIO, reinterpret_cast<u_long*>(&nonBlocking));
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
  auto result = ::setsockopt(mHandle, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l), sizeof(l));
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::linger() -> StdResult<Optional<std::chrono::seconds>>
{
  struct linger l;
  int len = sizeof(l);
  auto result = ::getsockopt(mHandle, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&l), &len);
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
  auto result = ::setsockopt(mHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&enable), sizeof(enable));
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}
auto Socket::nodelay() -> StdResult<bool>
{
  int enable;
  int len = sizeof(enable);
  auto result = ::getsockopt(mHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&enable), &len);
  if (result == 0) {
    return enable != 0;
  } else {
    return make_unexpected(LastError());
  }
}

#undef RE
} // namespace io