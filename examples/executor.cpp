#include "io/Executor.hpp"
#include "io/async/Task.hpp"
#include "io/async/tcp.hpp"

int main()
{
  auto i = new int[1];
  i[0] = 233;
  auto executor = io::Executor {};
  executor.blockOn([&]() -> Task<> {
    auto listener = io::async::TcpListener::Bind(executor, io::SocketAddrV4 {io::ANY, 2333});
    if (!listener) {
      LOG_ERROR("bind failed: {}", listener.error().message());
      co_return;
    }
    for (int i = 0; i < 1000; i++) {
      auto stream = co_await listener->accept();
      if (!stream) {
        LOG_ERROR("accept failed: {}", stream.error().message());
        co_return;
      } else {
        // LOG_INFO("accept success, stream fd: {}", stream.value().socket().raw());
      }
      executor.spawn([&](io::async::TcpStream stream) mutable -> Task<> {
        // LOG_INFO("spawned task: fd: {}", stream.socket().raw());
        char buf[] = "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html; charset=UTF-8\r\n"
                     "Connection: keep-alive\r\n"
                     "Content-Length: 11\r\n"
                     "\r\n"
                     "hello world";
        // LOG_INFO("begin read");
        auto recvBuf = std::array<u8, 1024> {};
        auto rlen = co_await stream.read(recvBuf.data(), recvBuf.size());
        if (rlen) {
        } else {
          co_return;
        }
        auto len = co_await stream.write(buf, std::size(buf));
        // LOG_INFO("after read");
        if (!len) {
          LOG_ERROR("read failed: {}", len.error().message());
          co_return;
        } else {
          // LOG_INFO("read {} bytes", len.value());
        }
        co_return;
      }(std::move(stream.value())));
    }
    LOG_CRITICAL("main task exit");
  }());
}

int main1()
{
  auto executor = io::Executor {};
  auto i = executor.blockOn([&executor]() -> Task<int> {
    LOG_INFO("hello world");
    std::this_thread::sleep_for(1s);
    executor.spawn([]() -> Task<> {
      LOG_INFO("spawned task");
      std::this_thread::sleep_for(3s);
      co_return;
    }());
    co_return 233;
  }());
  LOG_INFO("i: {}", i);
}