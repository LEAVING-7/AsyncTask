#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;
int main()
{
  using RuntimeType = async::Runtime<async::InlineExecutor>;
  RuntimeType::Init();

  RuntimeType::Block([]() -> async::Task<> {
    auto t1 = async::JoinHandle([]() -> async::Task<> {
      puts("t1 start");
      co_await RuntimeType::Sleep((5s));
      puts("t1 end");
    }());
    RuntimeType::Spawn(t1);
    auto t2 = async::JoinHandle([]() -> async::Task<> {
      puts("t2 start");
      co_await RuntimeType::Sleep((2s));
      puts("t2 end");
    }());
    RuntimeType::Spawn(t2);

    co_await t1.join();
    puts("t1 joined");
    co_await t2.join();
    puts("t2 joined");
  }());
}