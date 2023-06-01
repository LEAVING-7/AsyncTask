add_executable(threadpool threadpool.cpp)
target_link_libraries(threadpool AsyncTask)

add_executable(executor executor.cpp)
target_link_libraries(executor AsyncTask)

add_executable(condvar condvar.cpp)
target_link_libraries(condvar AsyncTask)