add_executable(threadpool threadpool.cpp)
target_link_libraries(threadpool AsyncTask)

add_executable(executor executor.cpp)
target_link_libraries(executor AsyncTask)

add_executable(global_executor global_executor.cpp)
target_link_libraries(global_executor AsyncTask)