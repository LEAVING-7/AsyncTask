add_executable(threadpool threadpool.cpp)
target_link_libraries(threadpool AsyncTask)

add_executable(executor executor.cpp)
target_link_libraries(executor AsyncTask)

add_executable(primitive primitive.cpp)
target_link_libraries(primitive AsyncTask)