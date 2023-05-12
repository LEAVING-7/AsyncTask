enable_testing()
AddExternal(gtest; google/googletest; v1.13.0)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_executable(slab_test slab_test.cpp)
target_link_libraries(slab_test PUBLIC gtest_main AsyncTask)