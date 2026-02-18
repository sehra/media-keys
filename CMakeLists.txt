cmake_minimum_required(VERSION 3.15)
project(MediaKeys)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MediaKeys WIN32
    src/MediaKeys.cpp
    src/MediaKeys.rc
)

target_compile_definitions(MediaKeys PRIVATE UNICODE _UNICODE)
