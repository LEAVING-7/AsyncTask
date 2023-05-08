#include "io/async/tcp.hpp"
#include "platform.hpp"

namespace io::async {

#define IO_AWAITER_TEMPLATE(fn, awaiter)                                                                               \
  auto r = fn;                                                                                                         \
  if (!r &&                                                                                                            \
      (r.error() == std::errc::resource_unavailable_try_again || r.error() == std::errc::operation_would_block)) {     \
    co_await awaiter;                                                                                                  \
    return fn;                                                                                                         \
  } else {                                                                                                             \
    return r;                                                                                                          \
  }

struct ReadableAwaiter {
  Reactor* reactor;
  Source* source;

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) noexcept
  {
    auto r = source->setReadable(h);
    assert(r);
    reactor->updateIo(*source);
  }
  void await_resume() noexcept {}
};

struct WritableAwaiter {
  Reactor* reactor;
  Source* source;

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> h) noexcept
  {
    auto r = source->setWritable(h);
    assert(r);
    reactor->updateIo(*source);
  }
  void await_resume() noexcept {}
};

auto TcpStream::Connect(io::Reactor* reactor, SocketAddr const& addr) -> StdResult<TcpStream>
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
  return TcpStream {reactor, std::move(*socket)};
}
auto TcpStream::read(void* buf, size_t len) -> Task<StdResult<size_t>>
{
  auto r = Socket(mSource->fd).recv(buf, len, MSG_DONTWAIT);
  if (!r && (r.error() == std::errc::resource_unavailable_try_again || r.error() == std::errc::operation_would_block)) {
    co_await ReadableAwaiter {mReactor, mSource.get()};
    co_return Socket(mSource->fd).recv(buf, len, MSG_DONTWAIT);
  } else {
    co_return r;
  }
};
auto TcpStream::write(void const* buf, size_t len) -> Task<StdResult<size_t>>
{
  auto r = Socket(mSource->fd).send(buf, len, MSG_DONTWAIT);
  if (!r && (r.error() == std::errc::resource_unavailable_try_again || r.error() == std::errc::operation_would_block)) {
    co_await WritableAwaiter {mReactor, mSource.get()};
    co_return Socket(mSource->fd).send(buf, len, MSG_DONTWAIT);
  } else {
    co_return r;
  }
}
auto TcpListener::Bind(io::Reactor* reactor, SocketAddr const& addr) -> StdResult<TcpListener>
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
  return TcpListener {reactor, std::move(*socket)};
}
auto TcpListener::accept(SocketAddr* addr) -> Task<StdResult<TcpStream>>
{
  assert(mReactor);
  if (addr == nullptr) {
    auto r = ::accept4(mSource->fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (r == -1) {
      if (io::LastError() == std::errc::resource_unavailable_try_again ||
          io::LastError() == std::errc::operation_would_block) {
        co_await ReadableAwaiter {mReactor, mSource.get()};
        r = ::accept4(mSource->fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (r == -1) {
          LOG_INFO("accept4r: {}", strerror(errno));
        }
        assert(r != -1);
        co_return TcpStream {mReactor, Socket {r}};
      } else {
        co_return make_unexpected(io::LastError());
      }
    } else {
      co_return TcpStream {mReactor, Socket {r}};
    }
  } else {
    assert(0);
  }
  // assert(mExecutor);
  // if (addr != nullptr) {
  //   assert(0);
  //   sockaddr_storage storage;
  //   socklen_t len = sizeof(storage);
  //   auto r = io::SocketAddrToSockAddr(*addr, &storage, &len);
  //   assert(r);
  //   auto accept4r = ::accept(mSocket.raw(), (sockaddr*)&storage, &len);
  //   if (accept4r != -1) {
  //     LOG_INFO("accept4r: {}", strerror(errno));
  //     assert(accept4r != -1);
  //   }
  //   co_return TcpStream {mExecutor, Socket {accept4r}};
  // } else {
  //   auto accept4r = ::accept4(mSocket.raw(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
  //   if (accept4r == -1) {
  //     if (io::LastError() == std::errc::resource_unavailable_try_again ||
  //         io::LastError() == std::errc::operation_would_block) {
  //       co_await ReadableAwaiter {*mExecutor, mSocket.raw()};
  //       accept4r = ::accept4(mSocket.raw(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
  //       if (accept4r == -1) {
  //         LOG_INFO("accept4r: {}", strerror(errno));
  //       }
  //       assert(accept4r != -1);
  //       co_return TcpStream {mExecutor, Socket {accept4r}};
  //     }
  //     co_return make_unexpected(io::LastError());
  //   } else {
  //     co_return TcpStream {mExecutor, Socket {accept4r}};
  //   }
  // }
}
} // namespace io::async