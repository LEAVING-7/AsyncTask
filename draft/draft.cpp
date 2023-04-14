#include <mutex>

#include "io/Slab.hpp"
#include "io/net/SocketAddr.hpp"
#include "io/sys/common/tcp.hpp"
#include <iostream>

struct Foo {
  Foo() { puts("default ctor"); }
  Foo(int in_v) : v(in_v) { puts("foo ctor"); }
  Foo(Foo const&) { puts("foo copy ctor"); }
  Foo(Foo&&) { puts("foo mv ctor"); };
  ~Foo() { puts("dtor"); }
  int v;
};

int main(int argc, char* argv[])
{
  auto listener = io::TcpListener::Bind(io::SocketAddrV4 {io::ANY, 2333});
  if (!listener) {
    return 1;
  }
  auto stream = listener->accept(nullptr);
  if (!stream) {
    return 1;
  }
  char buf[1024];
  auto result = stream->read(buf, 1024);
  if (!result) {
    return 1;
  }
  buf[1023] = '\0';
  printf("%s", buf);

  return 0;
}