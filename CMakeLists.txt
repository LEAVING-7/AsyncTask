cmake_minimum_required(VERSION 3.20)
project(AsyncTask VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "-fcolor-diagnostics ${CMAKE_CXX_FLAGS}")
endif()

include(FetchContent)
macro(AddExternal target url git_tag)
  FetchContent_Declare(
    ${target}
    GIT_REPOSITORY https://github.com/${url}
    GIT_TAG ${git_tag}
  )
  FetchContent_MakeAvailable(${target})
endmacro()

file(GLOB SOURCES 
      "lib/*.cpp" 
      "lib/sys/*.cpp" 
      "lib/sys/unix/*.cpp" 
      "lib/sys/win/*.cpp" 
      "lib/utils/*.cpp"
)

add_library(AsyncTask OBJECT)
target_sources(AsyncTask PRIVATE ${SOURCES})
target_include_directories(AsyncTask  
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "-fsanitize=address ${CMAKE_CXX_FLAGS}") 
    target_compile_definitions(AsyncTask PUBLIC DEBUG)
endif()

option(ASYNC_TASK_BUILD_EXAMPLES "Build examples" OFF)
option(ASYNC_TASK_BUILD_TESTS "Build tests" OFF)

if(ASYNC_TASK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(ASYNC_TASK_BUILD_TESTS)
    add_subdirectory(tests)
endif()