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
  std::variant<int, long> v = 123;
  std::cout << v.index() << '\n';
  // assert(slab.isEmpty());
  // auto hello = slab.insert(123);
  // assert(slab[hello].v == 123);
  // assert(slab.get(hello)->v == 123);
  // assert(!slab.isEmpty());
  // assert(slab.contains(key));
  // auto v = slab.tryRemove(hello);
  // assert(v.has_value());
  // assert(v->v == 123);
  return 0;
}