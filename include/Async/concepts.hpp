#include <concepts>
#include <coroutine>
namespace async {
template <typename T>
concept ExecutorCpt = requires(T&& executor) {
                        {
                          executor.execute(std::declval<std::coroutine_handle<>>())
                          } -> std::same_as<void>;
                      };
}