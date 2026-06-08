cmake_minimum_required(VERSION 3.20)

# 使用 fetchcontent 获取 GTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ---- SPSCQueue 测试 ----
add_executable(test_spsc_queue test_spsc_queue.cpp)
target_link_libraries(test_spsc_queue PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
    pthread
)
gtest_discover_tests(test_spsc_queue)

# ---- PacketParser 测试 ----
add_executable(test_packet_parser test_packet_parser.cpp)
target_link_libraries(test_packet_parser PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
    pthread
)
gtest_discover_tests(test_packet_parser)

# ---- PcapCapture 测试 ----
add_executable(test_pcap_capture test_pcap_capture.cpp)
target_link_libraries(test_pcap_capture PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
    pthread
)
gtest_discover_tests(test_pcap_capture)

# ---- AhoCorasick 测试 ----
add_executable(test_aho_corasick test_aho_corasick.cpp)
target_link_libraries(test_aho_corasick PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_aho_corasick)

# ---- Pipeline 测试 ----
add_executable(test_pipeline test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
    pthread
)
gtest_discover_tests(test_pipeline)

# ---- ObjectPool 测试 ----
add_executable(test_object_pool test_object_pool.cpp)
target_link_libraries(test_object_pool PRIVATE
    sentinel_core
    GTest::gtest
    GTest::gtest_main
    pthread
)
gtest_discover_tests(test_object_pool)
