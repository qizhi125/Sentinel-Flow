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
        "src/capture/driver/*.c"
        "src/capture/driver/*.cpp"
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

set(SRC_MAIN "src/main.cpp"
        )

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
        bpf
        xdp
        elf
        z
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

find_program(CLANG_EXE clang REQUIRED)

set(XDP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/driver/xdp_prog.c)
set(XDP_OBJ ${CMAKE_CURRENT_BINARY_DIR}/xdp_prog.o)

add_custom_command(
        OUTPUT ${XDP_OBJ}
        COMMAND ${CLANG_EXE} -g -O2 -target bpf -c ${XDP_SRC} -o ${XDP_OBJ}
        DEPENDS ${XDP_SRC}
        COMMENT "Compiling eBPF program: xdp_prog.c -> xdp_prog.o"
)

add_custom_target(ebpf_objects ALL DEPENDS ${XDP_OBJ})

add_custom_command(TARGET ebpf_objects POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${XDP_OBJ} $<TARGET_FILE_DIR:SentinelApp>
)

add_dependencies(SentinelApp ebpf_objects)