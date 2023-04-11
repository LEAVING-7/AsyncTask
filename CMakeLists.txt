cmake_minimum_required(VERSION 3.20)

project(io)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES 
      "lib/*.cpp" 
      "lib/net/*.cpp" 
      "lib/sys/common/*.cpp" 
      "lib/sys/unix/*.cpp" 
      "lib/sys/win/*.cpp" 
      "lib/utils/*.cpp"
)

add_library(io STATIC)
target_sources(io PRIVATE ${SOURCES})
target_include_directories(io 
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/utils      
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)
if(WIN32)
    target_link_libraries(io ws2_32)
endif()

add_executable(draft draft/draft.cpp)
target_link_libraries(draft io)