cmake_minimum_required(VERSION 3.20)
project(SentinelFlow CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(-Wall -Wextra -Werror)

add_subdirectory(libsentinel)

enable_testing()
add_subdirectory(tests)
