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
async::Task is the basic structure of the entire AsyncTask, which allows you to use co_await expressions on it.
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
### InlineExecutor
async::InlineExecutor puts all ready coroutines on the current thread for execution, which means no additional thread synchronization is needed between coroutines, avoiding the overhead of thread switching.
```
  
```
### MultiThreadExecutor

### Mutex
### CondVar


[task.note]: https://en.cppreference.com/w/cpp/language/coroutines#Execution
[badge.license]: https://img.shields.io/github/license/LEAVING-7/AsyncTask
[badge.language]: https://img.shields.io/badge/language-C%2B%2B20-yellow.svg

[language]: https://en.wikipedia.org/wiki/C%2B%2B20
[license]: https://en.wikipedia.org/wiki/Apache_License