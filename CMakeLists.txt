cmake_minimum_required(VERSION 3.15)
project(MediaKeys)

add_executable(MediaKeys WIN32
    src/MediaKeys.cpp
    src/MediaKeys.rc
    src/MediaKeys.manifest
    src/UniqueHandle.h
)

target_compile_features(MediaKeys PRIVATE cxx_std_20)
target_compile_definitions(MediaKeys PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
