#include <Async/Task.hpp>
#include <cstdio>
using namespace async;
int main()
{
  auto task = []() -> Task<int32_t> { co_return 42; };
  auto task1 = [&task](int32_t x) -> Task<int32_t> { co_return x*(co_await task()); };
  auto co = task1(2);
  while (!co.done()) {
    co.resume();
  }
  assert(co.promise().result() == 84);
}