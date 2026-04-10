cmake_minimum_required(VERSION 3.20)
project(SentinelFlow CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# 开启 CTest 测试支持
enable_testing()

# 编译数据面核心库
add_subdirectory(libsentinel)

# 编译单元测试
add_subdirectory(tests)
