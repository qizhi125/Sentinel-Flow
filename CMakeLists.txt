cmake_minimum_required(VERSION 3.20)
project(SentinelFlowCore VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -fPIC)

# 告诉 CMake 去哪里找我们自定义的 FindPCAP.cmake 模块
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

find_package(PCAP REQUIRED)

add_subdirectory(libsentinel)
