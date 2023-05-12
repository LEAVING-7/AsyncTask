cmake_minimum_required(VERSION 3.20)

project(Async)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-fsanitize=address -fcolor-diagnostics")

file(GLOB SOURCES 
      "lib/*.cpp" 
      "lib/sys/*.cpp" 
      "lib/sys/unix/*.cpp" 
      "lib/sys/win/*.cpp" 
      "lib/utils/*.cpp"
)

add_library(AsyncTask STATIC)
target_sources(AsyncTask PRIVATE ${SOURCES})
target_include_directories(AsyncTask 
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/utils      
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(AsyncTask PUBLIC spdlog::spdlog)

enable_testing()

include(FetchContent)

macro(AddExternal target url git_tag)
  FetchContent_Declare(
    ${target}
    GIT_REPOSITORY https://ghproxy.com/https://github.com/${url}
    GIT_TAG ${git_tag}
  )
  FetchContent_MakeAvailable(${target})
endmacro()

AddExternal(spdlog; gabime/spdlog; v1.11.0)
AddExternal(gtest; google/googletest; v1.13.0)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(tests)
add_subdirectory(examples)