cmake_minimum_required(VERSION 3.28)

project(streambuf VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
    add_compile_definitions(_WIN32_WINNT=0x0601)
    add_compile_definitions(NOMINMAX)
endif()

set(CMAKE_CXX_STANDARD 23)

add_library(streambuf INTERFACE)
target_include_directories(streambuf INTERFACE include)
target_precompile_headers(streambuf INTERFACE include/streambuf.hpp)

find_package(Boost REQUIRED)
target_link_libraries(streambuf INTERFACE Boost::boost)

add_executable(streambuf_test src/test.cpp)
target_link_libraries(streambuf_test PRIVATE streambuf)

add_executable(streambuf_async_test src/test_async.cpp)
target_link_libraries(streambuf_async_test PRIVATE streambuf)
