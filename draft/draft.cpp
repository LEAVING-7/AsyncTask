#include <mutex>

#include <iostream>

#include <inttypes.h>
#include <iostream>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <variant>

int main(int argc, char* argv[])
{
  struct Foo {
    struct Foo1 {
      int a;
      int b;
    };
    struct Foo2 {
      std::string name;
    };

    std::variant<Foo1, Foo2> v;
  };

  Foo foo = {Foo::Foo1 {1, 2}};
}