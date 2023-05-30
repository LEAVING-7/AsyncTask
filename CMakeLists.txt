cmake_minimum_required(VERSION 3.20)
project(AsyncTask VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "-Wall -fcolor-diagnostics ${CMAKE_CXX_FLAGS}")
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
  set(CMAKE_CXX_FLAGS "-lstdc++ -g -lm -Wall -fdiagnostics-color=always ${CMAKE_CXX_FLAGS}")
endif()

include(FetchContent)
macro(AddExternal target url git_tag)
  if(NOT DEFINED GLOBAL_GITHUB_ADDRESS)
    set(GLOBAL_GITHUB_ADDRESS "https://github.com")
  endif()
  FetchContent_Declare(
    ${target}
    GIT_REPOSITORY "${GLOBAL_GITHUB_ADDRESS}/${url}"
    GIT_TAG ${git_tag}
  )
FetchContent_MakeAvailable(${target})
endmacro()

file(GLOB AsyncTask_SOURCE 
      "lib/*.cpp" 
      "lib/sys/*.cpp" 
      "lib/sys/unix/*.cpp" 
      "lib/sys/win/*.cpp" 
      "lib/utils/*.cpp"
)

add_library(AsyncTask STATIC)
target_sources(AsyncTask PRIVATE ${AsyncTask_SOURCE})
target_include_directories(AsyncTask  
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "-fsanitize=address ${CMAKE_CXX_FLAGS}") 
    target_compile_definitions(AsyncTask PUBLIC DEBUG)
endif()

option(AsyncTask_BUILD_EXAMPLES "Build examples" ON)
option(AsyncTask_BUILD_TESTS "Build tests" OFF)

if(AsyncTask_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(AsyncTask_BUILD_TESTS)
    add_subdirectory(tests)
endif()