#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;

auto inlineExecutor() -> void
{
  using RT = async::Runtime<async::InlineExecutor>;
  RT::Init();
  RT::Block([]() -> async::Task<> {
    puts("hello world");
    auto v = co_await RT::BlockSpawn([]() -> int32_t {
      puts("begin sleep");
      std::this_thread::sleep_for(3s);
      puts("wake up");
      return 42;
    });
    assert(v == 42);
    auto t1 = async::JoinHandle([]() -> async::Task<int> {
      puts("t1 start");
      co_await RT::Sleep(5s);
      puts("t1 end");
      co_return 24;
    }());
    RT::Spawn(t1);
    assert(co_await t1.join() == 24);
    RT::SpawnDetach([]() -> async::Task<> {
      puts("t2 start");
      co_await RT::Sleep(2s);
      puts("t2 end");
    }());
  }());
}

int main() { inlineExecutor(); }