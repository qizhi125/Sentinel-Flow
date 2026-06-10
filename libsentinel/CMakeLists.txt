cmake_minimum_required(VERSION 3.20)
project(libsentinel)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

add_library(sentinel_core STATIC
    src/engine/flow/PacketParser.cpp
    src/capture/PcapCapture.cpp
    src/engine/match/AhoCorasick.cpp
    src/engine/Pipeline.cpp
    src/engine/DatabaseManager.cpp
    src/capi_impl.cpp
)

target_include_directories(sentinel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PCAP_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(sentinel_core PUBLIC
    stdc++
    ${PCAP_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    Threads::Threads
)
