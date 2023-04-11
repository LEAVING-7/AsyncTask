#include "io/sys/common/tcp.hpp"

namespace io {
auto TcpStream::Connect(SocketAddr const& addr) -> StdResult<TcpStream>
{
  io::Init();
  auto socket = CreateSocket(addr, SOCK_STREAM);

  if (!socket) {
    return make_unexpected(socket.error());
  }
  auto result = socket->connect(addr);
  if (!result) {
    return make_unexpected(result.error());
  }
  return TcpStream {std::move(*socket)};
}

auto TcpStream::write(void const* buf, size_t len) -> StdResult<size_t>
{
  auto result = ::send(mSocket.raw(), static_cast<char const*>(buf), len, 0);
  if (result >= 0) {
    return result;
  } else {
    return make_unexpected(LastError());
  }
}

auto TcpListener::Bind(SocketAddr const& addr) -> StdResult<TcpListener>
{
  io::Init();
  if (auto socket = CreateSocket(addr, SOCK_STREAM); socket) {
#ifndef WIN_PLATFORM
    ::setsockopt(socket->raw(), SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    sockaddr_storage storage;
    int len;
    if (auto r = SocketAddrToSockAddr(addr, &storage, &len); !r) {
      return make_unexpected(r.error());
    } else {
      if (::bind(socket->raw(), reinterpret_cast<sockaddr*>(&storage), len) == 0) {
        if (::listen(socket->raw(), 128) == 0) {
          return TcpListener {std::move(*socket)};
        } else {
          return make_unexpected(LastError());
        }
      } else {
        return make_unexpected(LastError());
      }
    }
  } else {
    return make_unexpected(socket.error());
  }
}

auto TcpListener::accept(SocketAddr* addr) -> StdResult<TcpStream>
{
  if (addr != nullptr) {
    sockaddr_storage storage;
    int len = sizeof(storage);
    if (auto r = io::SocketAddrToSockAddr(*addr, &storage, &len); !r) {
      return make_unexpected(r.error());
    }
    auto r = ::accept(mSocket.raw(), reinterpret_cast<sockaddr*>(&storage), &len);
    if (r >= 0) {
      return TcpStream {Socket {r}};
    } else {
      return make_unexpected(LastError());
    }
  } else {
    auto r = ::accept(mSocket.raw(), nullptr, nullptr);
    if (r >= 0) {
      return TcpStream {Socket {r}};
    } else {
      return make_unexpected(LastError());
    }
  }
}
} // namespace io