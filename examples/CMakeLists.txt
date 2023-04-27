add_executable(poller poller.cpp)
target_link_libraries(poller io)

add_executable(threadpool threadpool.cpp)
target_link_libraries(threadpool io)

add_executable(executor executor.cpp)
target_link_libraries(executor io)