#include "io/Executor.hpp"
#include "io/async/Task.hpp"
#include "io/async/tcp.hpp"
using namespace std::chrono_literals;
// int main()
// {
//   auto executor = Executor {};
//   auto reactor = io::Reactor();
//   LOG_INFO("before blockOn");
//   auto future = executor.blockOn(
//       [](io::Reactor& reactor, Executor& e) -> Task<> {
//         LOG_INFO("hello world");
//         auto listener = io::async::TcpListener::Bind(&reactor, io::SocketAddrV4 {io::ANY, 2333});
//         if (!listener) {
//           LOG_ERROR("bind failed: {}", listener.error().message());
//           co_return;
//         } else {
//           LOG_INFO("bind success");
//         }
//         for (int i = 0;; i++) {
//           auto stream = co_await listener->accept();
//           if (!stream) {
//             LOG_ERROR("accept failed: {}", stream.error().message());
//             co_return;
//           } else {
//             LOG_INFO("accept success, stream fd: {}", stream.value().socket().raw());
//           }

//           e.spawn([](io::async::TcpStream stream, Executor& e) mutable -> Task<> {
//             LOG_INFO("spawned");
//             char buf[] = "HTTP/1.1 200 OK\r\n"
//                          "Content-Type: text/html; charset=UTF-8\r\n"
//                          "Connection: keep-alive\r\n"
//                          "Content-Length: 11\r\n"
//                          "\r\n"
//                          "hello world";
//             // LOG_INFO("begin read");
//             auto recvBuf = std::array<u8, 1024> {};
//             LOG_WARN("before read");
//             auto rlen = co_await stream.read(recvBuf.data(), recvBuf.size());
//             LOG_WARN("after read");
//             if (rlen) {
//             } else {
//               co_return;
//             }
//             auto len = co_await stream.write(buf, std::size(buf));
//             // LOG_INFO("after read");
//             if (!len) {
//               LOG_ERROR("read failed: {}", len.error().message());
//               co_return;
//             } else {
//               // LOG_INFO("read {} bytes", len.value());
//             }
//           }(std::move(stream).value(), e));
//         }
//         LOG_CRITICAL("main task exit");
//         co_return;
//       }(reactor, executor),
//       reactor);
//   while (future.wait_for(0s) != std::future_status::ready && !executor.isEmpty()) {
//     reactor.lock().react(std::nullopt, executor);
//   }
//   executor.waitEmpty();
//   LOG_INFO("main thread exit");
// }

/* int main1()
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
} */

int main()
{
  auto e = io::Executor {};
  auto r = io::Reactor {};
  auto f = e.blockOn(
      [](io::Executor& e, io::Reactor& r) -> Task<> {
        LOG_INFO("before sleep");
        co_await r.sleep(2s);
        LOG_INFO("after sleep");
        co_return;
      }(e, r),
      r);
  while (true) {
    if (f.wait_for(0ns) == std::future_status::ready) {
      LOG_INFO("future ready");
      break;
    }
    r.lock().react(std::nullopt, e);
  }
  e.waitEmpty();
  LOG_INFO("main thread exit");
}