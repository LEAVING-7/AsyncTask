#pragma once

#include "io/Executor.hpp"
#include "io/net/tcp.hpp"

namespace io::async {
class TcpStream {
public:
  TcpStream() = default;
  TcpStream(io::Executor* executor, Socket&& socket) : mSocket(std::move(socket)), mExecutor(executor) {}
  TcpStream(TcpStream const&) = delete;
  TcpStream& operator=(TcpStream const&) = delete;
  TcpStream(TcpStream&& other) : mSocket(std::move(other.mSocket)), mExecutor(other.mExecutor) {};
  TcpStream& operator=(TcpStream&& other) noexcept
  {
    mSocket = std::move(other.mSocket);
    mExecutor = other.mExecutor;
    return *this;
  };

  ~TcpStream()
  {
    if (mSocket.valid()) {
      mSocket.close();
    }
  }

  static auto Connect(io::Executor& e, SocketAddr const& addr) -> StdResult<TcpStream>;

  auto read(void* buf, size_t len) -> Task<StdResult<size_t>>;
  auto write(void const* buf, size_t len) -> Task<StdResult<size_t>>;

  auto setLinger(bool enable, int timeout) -> StdResult<void> { return mSocket.setLinger(enable, timeout); }
  auto linger() -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() -> StdResult<bool> { return mSocket.nodelay(); }
  auto socket() const -> io::Socket const& { return mSocket; }

  auto close() -> StdResult<void> { return mSocket.close(); }

private:
  io::Socket mSocket;
  io::Executor* mExecutor {nullptr};
};

class TcpListener {
public:
  TcpListener(io::Executor* executor, Socket&& socket) : mSocket(std::move(socket)), mExecutor(executor) {}
  TcpListener(TcpListener&& other) = default;
  TcpListener& operator=(TcpListener&& other) = default;
  ~TcpListener()
  {
    if (mSocket.valid()) {
      mSocket.close();
    }
  }

  static auto Bind(io::Executor& e, SocketAddr const& addr) -> StdResult<TcpListener>;
  auto accept(SocketAddr* addr = nullptr) -> Task<StdResult<TcpStream>>;

  auto setLinger(bool enable, int timeout) -> StdResult<void> { return mSocket.setLinger(enable, timeout); }
  auto linger() const -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() const -> StdResult<bool> { return mSocket.nodelay(); }
  auto socket() const -> io::Socket const& { return mSocket; }

  auto close() -> StdResult<void> { return mSocket.close(); }

private:
  io::Socket mSocket;
  io::Executor* mExecutor;
};
} // namespace io::async