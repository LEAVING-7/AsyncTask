#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;
int main()
{
  using RT = async::Runtime<async::MultiThreadExecutor>;
  RT::Init(4);

  RT::Block([]() -> async::Task<> {
    auto t1 = async::JoinHandle<int>([]() -> async::Task<int> {
      puts("t1 start");
      co_await RT::Sleep((5s));
      puts("t1 end");
      co_return 1;
    }());
    RT::Spawn(t1);
    auto t2 = async::JoinHandle([]() -> async::Task<> {
      puts("t2 start");
      co_await RT::Sleep((2s));
      puts("t2 end");
    }());
    RT::Spawn(t2);
    auto k = co_await t1.join();
    printf("t1 return %d\n", k);
    co_await RT::WaitAll(t2);
    puts("every thing done");
  }());
}