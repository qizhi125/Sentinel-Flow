cmake_minimum_required(VERSION 3.20)

# 自动获取 Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)
FetchContent_MakeAvailable(googletest)

add_executable(sentinel_tests test_core_engine.cpp)

# 链接核心引擎和 GTest 框架
target_include_directories(sentinel_tests PRIVATE 
    ${CMAKE_SOURCE_DIR}/libsentinel/include
    ${CMAKE_SOURCE_DIR}/libsentinel/src
)

target_link_libraries(sentinel_tests PRIVATE 
    sentinel_core 
    GTest::gtest_main 
    pthread
)

include(GoogleTest)
gtest_discover_tests(sentinel_tests)
