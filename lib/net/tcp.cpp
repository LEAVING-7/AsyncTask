#include "io/net/tcp.hpp"

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

auto TcpStream::write(void const* buf, size_t len) -> StdResult<size_t> { return mSocket.send(buf, len, 0); }

auto TcpListener::Bind(SocketAddr const& addr) -> StdResult<TcpListener>
{
  io::Init();
  if (auto socket = CreateSocket(addr, SOCK_STREAM); socket) {
#ifndef WIN_PLATFORM
    int reuse = 1;
    ::setsockopt(socket->raw(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse), sizeof(reuse));
#endif
    sockaddr_storage storage;
    socklen_t len;
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
    socklen_t len = sizeof(storage);
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