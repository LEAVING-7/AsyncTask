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

#include <random>
int main()
{
  auto e = io::InlineExecutor {};
  auto r = io::Reactor {};
  auto now = std::chrono::steady_clock::now();
  e.block(
      [](io::InlineExecutor& e, io::Reactor& r) -> Task<> {
        e.spawn(
            [](io::Reactor& r) -> Task<> {
              LOG_CRITICAL("______ A task");
              co_await r.sleep(4s);
              LOG_CRITICAL("______ A task end");
              co_return;
            }(r),
            r);
        e.spawn(
            [](io::Reactor& r) -> Task<> {
              LOG_CRITICAL("______ B task");
              co_await r.sleep(7s);
              LOG_CRITICAL("______ B task end");
              co_return;
            }(r),
            r);
        for (int i = 0; i < 1000; i++) {
          e.spawn(
              [](int i, io::Reactor& r) -> Task<> {
                co_await r.sleep(8s);
                LOG_CRITICAL("______ C task {}", i);
                co_return;
              }(i, r),
              r);
        }
        co_return;
      }(e, r),
      r);
  auto done = std::chrono::steady_clock::now();
  LOG_INFO("elapsed: {}s", std::chrono::duration_cast<std::chrono::seconds>(done - now).count());
  LOG_INFO("main thread end, with return value: {}", 123);
  // std::this_thread::sleep_for(10s);
}