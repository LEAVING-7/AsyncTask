add_executable(example_threadpool example_threadpool.cpp)
target_link_libraries(example_threadpool AsyncTask)

add_executable(example_executor example_executor.cpp)
target_link_libraries(example_executor AsyncTask)

# add_executable(condvar condvar.cpp)
# target_link_libraries(condvar AsyncTask)

add_executable(example_spawn example_spawn.cpp)
target_link_libraries(example_spawn AsyncTask)