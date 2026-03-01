cmake_minimum_required(VERSION 3.20)
project(Sentinel-Flow VERSION 6.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Charts)
find_package(SQLite3 REQUIRED)
find_package(Curses QUIET)

file(GLOB_RECURSE SRC_COMMON
        "src/common/types/*.cpp"
        "src/common/queues/*.cpp"
)

file(GLOB_RECURSE SRC_CAPTURE
        "src/capture/impl/*.cpp"
)

file(GLOB_RECURSE SRC_ENGINE
        "src/engine/pipeline/*.cpp"
        "src/engine/flow/*.cpp"
        "src/engine/context/*.cpp"
)

file(GLOB_RECURSE SRC_PRESENTATION
        "src/presentation/views/*.cpp"
        "src/presentation/views/pages/*.cpp"
        "src/presentation/views/components/*.cpp"
        "src/presentation/cli/*.cpp"
        "src/presentation/adapters/*.cpp"
        "src/presentation/app/*.cpp"
)

set(SRC_MAIN "src/main.cpp")

add_executable(SentinelApp
        ${SRC_MAIN}
        ${SRC_COMMON}
        ${SRC_CAPTURE}
        ${SRC_ENGINE}
        ${SRC_PRESENTATION}
)

target_include_directories(SentinelApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# If curses was found, add its include dir and library and define HAVE_CURSES
if(CURSES_FOUND)
    target_include_directories(SentinelApp PRIVATE ${CURSES_INCLUDE_DIR})
    target_link_libraries(SentinelApp PRIVATE ${CURSES_LIBRARIES})
    target_compile_definitions(SentinelApp PRIVATE HAVE_CURSES)
endif()

target_link_libraries(SentinelApp PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Charts
        pcap
        SQLite::SQLite3
        atomic
)

set_property(TARGET SentinelApp PROPERTY AUTOMOC ON)
set_property(TARGET SentinelApp PROPERTY AUTOUIC ON)
set_property(TARGET SentinelApp PROPERTY AUTORCC ON)

enable_testing()
add_subdirectory(tests)

if(MSVC)
    target_compile_options(SentinelApp PRIVATE /W4)
else()
    target_compile_options(SentinelApp PRIVATE -Wall -Wextra)
    target_compile_options(SentinelApp PRIVATE
            $<$<CONFIG:Debug>:-O0 -g>
            $<$<CONFIG:Release>:-O3 -flto>
    )
endif()