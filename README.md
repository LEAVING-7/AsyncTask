# AsyncTask: A tiny C++20 coroutine library base on epoll
[![license][badge.license]][license]
[![language][badge.language]][language]

## Overview
* Coroutine wrapper
  - [async::Task](#Task)
* Executors
  - [async::MultiThreadExecutor](#MultiThreadExecutor) has a fixed-size thread pool.
  - [async::InlineExecutor](#InlineExecutor) execute coroutine in the main thread.
* Synchronization Primitives
  - [async::Mutex](#Mutex)
  - [async::CondVar](#CondVar)
* Reactor 
  - [async::Reactor](#Reactor)

## Usage
### About create async::Task
The lifetime of external parameters required to create a coroutine must be longer than the lifetime of the coroutine itself, otherwise a dangling reference may occur.
see [cppreference][task.note].

### Task
`async::Task` is the basic structure of the entire AsyncTask, which allows you to use co_await expressions on it.
```C++
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
```
### Executors
All Executors support the `spawn`, `spawnDetach`, and `blockSpawn` methods.
#### InlineExecutor
`async::InlineExecutor` puts all ready coroutines on the current thread for execution, which means no additional thread synchronization is needed between coroutines, avoiding the overhead of thread switching.


```C++
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
```
with output
```
$ ./example/example_executors
hello world
begin sleep
wake up
t1 start
t1 end
t2 start
t2 end
```
#### MultiThreadExecutor
It is used in the same way as `InlineExecutor`, but executes tasks on a fixed-size thread pool, so there is additional thread synchronization overhead.
### Mutex
`async::Mutex` uses an atomic variable and an `async::mpmc::Queue` to keep track of the state and all coroutines waiting on this mutex.

Note that the outermost Block method will wait for all coroutines created through SpawnDetach, so the following Mutex needs to use shared_ptr to ensure the Mutex keeps alive.
```C++
static int cnt = 0;
RT::Block([]() -> Task<> {
  auto mutex = std::make_shared<Mutex>();
  for (int i = 0; i < 1'000'000; i++) {
    RT::SpawnDetach([](auto mt) -> Task<> {
      co_await mt->lock();
      cnt++;
      if (cnt % 100000 == 0) {
        printf("cnt: %d\n", cnt);
      }
      mt->unlock();
      co_return;
    }(mutex));
  }
  co_return;
}());
```
### CondVar
`async::CondVar` is similar to `async::Mutex`. It puts all coroutines waiting on this condition variable into a waiting queue and wakes them up when notify_one or notify_all is called on the condition variable.
```C++
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
```

### Reactor
usage see: [AsyncIO][reactor.usage]

[reactor.usage]: https://github.com/LEAVING-7/AsyncIO
[task.note]: https://en.cppreference.com/w/cpp/language/coroutines#Execution
[badge.license]: https://img.shields.io/github/license/LEAVING-7/AsyncTask
[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg

[language]: https://en.wikipedia.org/wiki/C%2B%2B20
[license]: https://en.wikipedia.org/wiki/Apache_License