#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;

void t1()
{
  using RT = async::Runtime<async::MultiThreadExecutor>;
  RT::Block([]() -> async::Task<> {
    auto condVar = std::make_shared<async::CondVar>();
    RT::SpawnDetach([](auto condVar) -> async::Task<> {
      co_await condVar->wait();
      puts("t1 start");
      co_await RT::Sleep((5s));
      puts("t1 end");
      co_return;
    }(condVar));
    RT::SpawnDetach([](auto condVar) -> async::Task<> {
      co_await condVar->wait();
      puts("t3 start");
      co_await RT::Sleep((2s));
      puts("t3 end");
      co_return;
    }(condVar));
    RT::SpawnDetach([](auto condVar) -> async::Task<> {
      puts("t2 start");
      co_await RT::Sleep((2s));
      puts("t2 end");
      condVar->notify_all();
      co_return;
    }(condVar));
    puts("every thing done");
    co_return;
  }());
}
void t2()
{
  using RT = async::Runtime<async::MultiThreadExecutor>;
  RT::Block([]() -> async::Task<> {
    auto condVar = async::CondVar();
    auto t1 = async::JoinHandle([](auto& cv) -> async::Task<> {
      co_await cv.wait();
      puts("t1 start");
      co_await RT::Sleep((5s));
      puts("t1 end");
      co_return;
    }(condVar));
    auto t2 = async::JoinHandle([](auto& cv) -> async::Task<> {
      puts("t2 start");
      co_await RT::Sleep((2s));
      puts("t2 end");
      cv.notify_all();
      co_return;
    }(condVar));
    RT::Spawn(t1);
    RT::Spawn(t2);
    co_await RT::WaitAll(t1, t2);
  }());
}
int main()
{
  async::Runtime<async::MultiThreadExecutor>::Init(4);
  t2();
}