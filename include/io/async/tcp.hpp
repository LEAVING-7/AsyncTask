#pragma once

#include "io/Reactor.hpp"
#include "io/async/Task.hpp"
#include "io/net/tcp.hpp"

namespace io::async {
class TcpStream {
public:
  TcpStream() = default;
  TcpStream(io::Reactor* reactor, io::Socket socket) : mReactor(reactor)
  {
    auto r = reactor->insertIo(socket.raw());
    assert(r);
    mSource = std::move(*r);
  }
  TcpStream(TcpStream&&) = default;
  TcpStream& operator=(TcpStream&&) = default;
  TcpStream(TcpStream const&) = delete;
  TcpStream& operator=(TcpStream const&) = delete;

  ~TcpStream() {}

  static auto Connect(io::Reactor& e, SocketAddr const& addr) -> StdResult<TcpStream>;

  auto read(void* buf, size_t len) -> Task<StdResult<size_t>>;
  auto write(void const* buf, size_t len) -> Task<StdResult<size_t>>;

  auto setLinger(bool enable, int timeout) -> StdResult<void> { return Socket().setLinger(enable, timeout); }
  auto linger() -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() -> StdResult<bool> { return mSocket.nodelay(); }
  auto socket() const -> io::Socket const& { return mSocket; }

  auto close() -> StdResult<void> { return mSocket.close(); }

private:
  std::shared_ptr<Source> mSource;
  io::Reactor* mReactor;
};

class TcpListener {
public:
  TcpListener(io::Reactor* reactor, Socket&& socket) : mSocket(std::move(socket)), mReactor(reactor) {}
  TcpListener(TcpListener&& other) = default;
  TcpListener& operator=(TcpListener&& other) = default;
  ~TcpListener() { mSocket.close(); }

  static auto Bind(io::Reactor& e, SocketAddr const& addr) -> StdResult<TcpListener>;
  auto accept(SocketAddr* addr = nullptr) -> Task<StdResult<TcpStream>>;

  auto setLinger(bool enable, int timeout) -> StdResult<void> { return mSocket.setLinger(enable, timeout); }
  auto linger() const -> StdResult<Optional<std::chrono::seconds>> { return mSocket.linger(); }
  auto setNoDelay(bool enable) -> StdResult<void> { return mSocket.setNoDelay(enable); }
  auto nodelay() const -> StdResult<bool> { return mSocket.nodelay(); }
  auto socket() const -> io::Socket const& { return mSocket; }

  auto close() -> StdResult<void> { return mSocket.close(); }

private:
  io::Socket mSocket;
  io::Reactor* mReactor;
};
} // namespace io::async