#include <Async/Executor.hpp>
#include <Async/Primitives.hpp>
#include <chrono>
using namespace std::chrono_literals;
using namespace async;

struct RT {
  std::unique_ptr<MultiThreadExecutor> e;
  std::unique_ptr<Reactor> r;
};
void case1(RT& rt)
{
  rt.e.get()->block(
      [](RT& rt) -> Task<> {
        auto condVar = CondVar(*rt.e);
        auto t1 = JoinHandle([](CondVar& cv, RT& rt) -> Task<> {
          co_await cv.wait();
          puts("t1 start");
          co_await rt.r->sleep(5s);
          puts("t1 end");
          co_return;
        }(condVar, rt));
        auto t3 = JoinHandle([](CondVar& cv, RT& rt) -> Task<> {
          co_await cv.wait();
          puts("t3 start");
          co_await rt.r->sleep(3s);
          puts("t3 end");
          co_return;
        }(condVar, rt));

        auto t2 = JoinHandle([](CondVar& cv, RT& rt) -> Task<> {
          puts("t2 start");
          co_await rt.r->sleep(2s);
          puts("t2 end");
          cv.notify_all();
          co_return;
        }(condVar, rt));

        rt.e->spawn(t1);
        rt.e->spawn(t2);
        rt.e->spawn(t3);

        co_await t1.join();
        co_await t2.join();
        co_await t3.join();
        co_return;
      }(rt),
      *rt.r);
}

int main()
{
  auto rt = RT {std::make_unique<MultiThreadExecutor>(4), std::make_unique<Reactor>()};
  case1(rt);
}