#pragma once
#include "platform.hpp"

#if defined(WIN_PLATFORM)
  #include "io/sys/win/Socket.hpp"
#elif defined(UNIX_PLATFORM)
  #include "io/sys/unix/Socket.hpp"
#endif
#include "log.hpp"

namespace io {
inline auto SetSockopt(Socket& socket, int level, int optname, void const* optval, socklen_t optlen) -> StdResult<void>
{
  auto result = ::setsockopt(socket.raw(), level, optname, static_cast<char const*>(optval), optlen);
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}

inline auto GetSockopt(Socket& socket, int level, int optname, void* optval, socklen_t* optlen) -> StdResult<void>
{
  auto result = ::getsockopt(socket.raw(), level, optname, static_cast<char*>(optval), optlen);
  if (result == 0) {
    return {};
  } else {
    return make_unexpected(LastError());
  }
}

class TcpStream {
public:
  TcpStream() = default;
  TcpStream(Socket&& socket) : mSocket(std::move(socket)) {}
  TcpStream(TcpStream const&) = delete;
  TcpStream(TcpStream&& other) noexcept : mSocket(std::move(other.mSocket)) {}
  TcpStream& operator=(TcpStream const&) = delete;
  TcpStream& operator=(TcpStream&& other) noexcept
  {
    mSocket = std::move(other.mSocket);
    return *this;
  };

  ~TcpStream()
  {
    LOG_INFO("TcpStream closed: {}", mSocket.raw());
    mSocket.close();
  }
  static auto Connect(SocketAddr const& addr) -> StdResult<TcpStream>;

  auto read(void* buf, size_t len) -> StdResult<size_t> { return mSocket.read(buf, len); }
  auto write(void const* buf, size_t len) -> StdResult<size_t>;

  auto setNonBlocking(bool nonBlocking) -> StdResult<void> { return mSocket.setNonBlocking(nonBlocking); }
  auto setLinger(bool enable, int timeout) -> StdResult<void> { return mSocket.setLinger(enable, timeout); }
  auto linger() -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() -> StdResult<bool> { return mSocket.nodelay(); }

  auto socket() const& -> Socket const& { return mSocket; }
  auto socket() && -> Socket&& { return std::move(mSocket); }

private:
  Socket mSocket;
};

class TcpListener {
public:
  TcpListener(Socket&& socket) : mSocket(std::move(socket)) {}
  TcpListener(TcpListener const&) = delete;
  TcpListener(TcpListener&& other) noexcept : mSocket(std::move(other.mSocket)) {}
  ~TcpListener() { mSocket.close(); }

  static auto Bind(SocketAddr const& addr) -> StdResult<TcpListener>;
  auto accept(SocketAddr* addr = nullptr) -> StdResult<TcpStream>;

  auto setNonBlocking(bool nonBlocking) -> StdResult<void> { return mSocket.setNonBlocking(nonBlocking); }
  auto setLinger(bool enable, int timeout) -> StdResult<void> { return mSocket.setLinger(enable, timeout); }
  auto linger() const -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() const -> StdResult<bool> { return mSocket.nodelay(); }

  auto socket() const& -> Socket const& { return mSocket; }
  auto socket() && -> Socket&& { return std::move(mSocket); }

private:
  Socket mSocket;
};

} // namespace io