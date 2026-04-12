cmake_minimum_required(VERSION 3.20)

if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/googletest/CMakeLists.txt)
    message(STATUS "使用本地 third_party/googletest")
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
    set(GTEST_LIBRARIES gtest gtest_main)
else()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "使用系统已安装的 Google Test")
        set(GTEST_LIBRARIES GTest::GTest GTest::Main)
    else()
        message(STATUS "本地与系统均无 GTest，将从 GitHub 下载")
        include(FetchContent)
        set(FETCHCONTENT_QUIET OFF)
        FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG        release-1.12.1
                GIT_PROGRESS   TRUE
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(googletest)
        set(GTEST_LIBRARIES gtest gtest_main)
    endif()
endif()

add_executable(sentinel_tests test_core_engine.cpp)

target_include_directories(sentinel_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/libsentinel/include
        ${CMAKE_SOURCE_DIR}/libsentinel/src
)

if(ENABLE_ASAN)
    target_link_options(sentinel_tests PRIVATE -fsanitize=address)
endif()
if(ENABLE_UBSAN)
    target_link_options(sentinel_tests PRIVATE -fsanitize=undefined)
endif()

target_link_libraries(sentinel_tests PRIVATE
        sentinel_core
        ${GTEST_LIBRARIES}
        pthread
)

include(GoogleTest)
gtest_discover_tests(sentinel_tests)