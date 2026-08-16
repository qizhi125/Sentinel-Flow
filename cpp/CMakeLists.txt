cmake_minimum_required(VERSION 3.16)
project(sentinel-core LANGUAGES CXX)

# 本文件仅供 IDE 索引 C++ 代码；正式构建仍由 crates/ffi/build.rs 驱动，
# 二者的编译参数保持一致（C++20、-Wall -Wextra -Werror、PIC）。
add_library(sentinel_core STATIC src/ja3.cpp)
target_include_directories(sentinel_core PUBLIC include)
target_compile_features(sentinel_core PUBLIC cxx_std_20)
set_target_properties(sentinel_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(sentinel_core PRIVATE -Wall -Wextra -Werror)
