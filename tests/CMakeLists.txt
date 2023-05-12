enable_testing()
add_executable(slab_test slab_test.cpp)
target_link_libraries(slab_test PUBLIC gtest_main AsyncTask)