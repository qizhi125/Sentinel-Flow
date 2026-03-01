# tests/CMakeLists.txt
find_package(GTest REQUIRED)

# 编译核心测试桩
add_executable(CoreEngineTests test_core_engine.cpp)

# 包含 src 目录，以便能找到 common 和 engine 的头文件
target_include_directories(CoreEngineTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 链接 GTest 库和多线程库
target_link_libraries(CoreEngineTests PRIVATE GTest::gtest GTest::gtest_main pthread atomic)

# 注册为 CTest
add_test(NAME CoreEngineTests COMMAND CoreEngineTests)