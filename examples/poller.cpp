#include <cstring>
#include <io/net/tcp.hpp>
#include <io/sys/Event.hpp>

#include <chrono>
#include <sys/timerfd.h>
using namespace std::chrono_literals;
auto GetKey(io::TcpListener const& listener) -> size_t { return listener.socket().raw(); }


int main()
{
  auto poller = io::Poller {};

  auto l1 = io::TcpListener::Bind(io::SocketAddrV4 {io::Ipv4Addr {127, 0, 0, 1}, 8080});
  auto l2 = io::TcpListener::Bind(io::SocketAddrV4 {io::Ipv4Addr {127, 0, 0, 1}, 8081});

  l1->setNonBlocking(true);
  l2->setNonBlocking(true);

  if (l1 && l2) {

    auto events = std::vector<io::Event> {};
    if (auto r = poller.add((l1.value().socket().raw()), io::Event::Readable(1)); !r) {
      std::cout << "poller add error: " << r.error().message() << '\n';
    };
    if (auto r = poller.add((l2.value().socket().raw()), io::Event::Readable(2)); !r) {
      std::cout << "poller add error: " << r.error().message() << '\n';
    };

    for (;;) {
      events.clear();
      auto len = poller.wait(events, std::nullopt);
      if (!len) {
        std::cout << "poller error: " << len.error().message() << std::endl;
        break;
      }
      for (auto& e : events) {
        std::cout << "recv event size: " << events.size() << " key: " << e.key << std::endl;
        if (e.key == 1) {
          auto c = l1->accept();
          if (c) {
            std::cout << "accept from 1" << std::endl;
            poller.mod(l1->socket().raw(), io::Event::Readable(1));
          } else {
            std::cout << "accept 1 error: " << c.error().message() << std::endl;
          }
        } else if (e.key == 2) {
          auto c = l2->accept();
          if (c) {
            std::cout << "accept from 2" << std::endl;
            poller.mod((l2->socket().raw()), io::Event::Readable(2));
          } else {
            std::cout << "accept 2 error: " << c.error().message() << std::endl;
          }
        }
      }
    }
  }
}