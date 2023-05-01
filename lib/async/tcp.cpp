#include "io/async/tcp.hpp"
#include "platform.hpp"

namespace io::async {
struct ReadableAwaiter {
  io::Executor& e;
  int fd;
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept
  {
    auto r = e.regReadableTask(fd, handle);
    if (!r) {
      LOG_INFO("reg readable failed: {}", r.error().message());
    }
  }
  void await_resume() noexcept
  {
    // auto r = e.unregTask(key);
    // if (!r) {
    //   LOG_INFO("unreg task failed: {}", r.error().message());
    // }
  }
};

struct WritableAwaiter {
  io::Executor& e;
  int fd;
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) noexcept
  {
    auto r = e.regWritableTask(fd, handle);
    if (!r) {
      LOG_INFO("reg readable failed: {}", r.error().message());
    }
  }
  void await_resume() noexcept
  {
    // auto r = e.unregTask(key);
    // if (!r) {
    //   LOG_INFO("unreg task failed: {}", r.error().message());
    // }
  }
};

auto TcpStream::Connect(io::Executor& e, SocketAddr const& addr) -> StdResult<TcpStream>
{
  auto socket = io::CreateSocket(addr, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC);
  // assert(socket);
  if (!socket) {
    return make_unexpected(socket.error());
  }
  auto r = socket->connect(addr);
  if (!r) {
    return make_unexpected(r.error());
  }
  return TcpStream {&e, std::move(*socket)};
}
auto TcpStream::read(void* buf, size_t len) -> Task<StdResult<size_t>>
{
  assert(mExecutor);
  assert(mSocket.valid());
  auto r = mSocket.recv(buf, len, MSG_DONTWAIT);
  if (!r && r.error() == std::errc::resource_unavailable_try_again) {
    co_await ReadableAwaiter {*mExecutor, mSocket.raw()};
    r = mSocket.recv(buf, len, 0);
    co_return std::move(r);
  } else {
    co_return std::move(r);
  }
};
auto TcpStream::write(void const* buf, size_t len) -> Task<StdResult<size_t>>
{
  assert(mExecutor);
  auto r = mSocket.send(buf, len, MSG_DONTWAIT);
  if (!r && r.error() == std::errc::resource_unavailable_try_again) {
    co_await WritableAwaiter {*mExecutor, mSocket.raw()};
    r = mSocket.send(buf, len, 0);
    co_return std::move(r);
  } else {
    co_return std::move(r);
  }
}
auto TcpListener::Bind(io::Executor& e, SocketAddr const& addr) -> StdResult<TcpListener>
{
  auto socket = io::CreateSocket(addr, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC);
  assert(socket);
#ifndef WIN_PLATFORM
  int reuse = 1;
  ::setsockopt(socket->raw(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_storage storage;
  socklen_t len;
  auto r = SocketAddrToSockAddr(addr, &storage, &len);
  if (!r) {
    return make_unexpected(r.error());
  }
  auto intr = ::bind(socket->raw(), (sockaddr*)&storage, len);
  if (intr == -1) {
    return make_unexpected(io::LastError());
  }
  intr = ::listen(socket->raw(), 1024);
  if (intr == -1) {
    return make_unexpected(io::LastError());
  }
  return TcpListener {&e, std::move(*socket)};
}
auto TcpListener::accept(SocketAddr* addr) -> Task<StdResult<TcpStream>>
{
  assert(mExecutor);
  if (addr != nullptr) {
    assert(0);
    sockaddr_storage storage;
    socklen_t len = sizeof(storage);
    auto r = io::SocketAddrToSockAddr(*addr, &storage, &len);
    assert(r);
    auto accept4r = ::accept(mSocket.raw(), (sockaddr*)&storage, &len);
    if (accept4r != -1) {
      LOG_INFO("accept4r: {}", strerror(errno));
      assert(accept4r != -1);
    }
    co_return TcpStream {mExecutor, Socket {accept4r}};
  } else {
    auto accept4r = ::accept4(mSocket.raw(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (accept4r == -1) {
      if (io::LastError() == std::errc::resource_unavailable_try_again ||
          io::LastError() == std::errc::operation_would_block) {
        co_await ReadableAwaiter {*mExecutor, mSocket.raw()};
        accept4r = ::accept4(mSocket.raw(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (accept4r == -1) {
          LOG_INFO("accept4r: {}", strerror(errno));
        }
        assert(accept4r != -1);
        co_return TcpStream {mExecutor, Socket {accept4r}};
      }
      co_return make_unexpected(io::LastError());
    } else {
      co_return TcpStream {mExecutor, Socket {accept4r}};
    }
  }
}
} // namespace io::async