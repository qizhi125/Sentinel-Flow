cmake_minimum_required(VERSION 3.20)
project(SentinelFlow CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_compile_options(-Wall -Wextra -Werror)

# ---- 第三方依赖 ----

include(FetchContent)

# spdlog — 异步无锁遥测日志
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.2
    SYSTEM
)
FetchContent_MakeAvailable(spdlog)

# google/benchmark — 吞吐量与延迟微基准测试
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.5
    SYSTEM
)
FetchContent_MakeAvailable(benchmark)

# ---- eBPF XDP 内核程序编译链 ----
# 步骤 1：clang -target bpf 编译 xdp_prog.c → xdp_prog.o
# 步骤 2：bpftool gen skeleton 生成 libbpf 骨架头文件 xdp_prog.skel.h

find_program(CLANG_EXECUTABLE clang REQUIRED
    DOC "clang 编译器（用于编译 eBPF 目标文件）")
message(STATUS "eBPF 编译器: ${CLANG_EXECUTABLE}")

find_program(BPFTOOL_EXECUTABLE bpftool REQUIRED
    DOC "bpftool 工具（用于生成 libbpf 骨架代码）")
message(STATUS "bpftool: ${BPFTOOL_EXECUTABLE}")

set(BPF_SOURCE_DIR  ${CMAKE_SOURCE_DIR}/bpf)
set(BPF_OUTPUT_DIR  ${CMAKE_BINARY_DIR}/bpf)
set(BPF_PROG_SRC    ${BPF_SOURCE_DIR}/xdp_prog.c)
set(BPF_PROG_OBJ    ${BPF_OUTPUT_DIR}/xdp_prog.o)
set(BPF_SKEL_H      ${BPF_OUTPUT_DIR}/xdp_prog.skel.h)

add_custom_command(
    OUTPUT ${BPF_PROG_OBJ} ${BPF_SKEL_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BPF_OUTPUT_DIR}
    COMMAND ${CLANG_EXECUTABLE}
        -O2 -g
        -target bpf
        -c ${BPF_PROG_SRC}
        -o ${BPF_PROG_OBJ}
    COMMAND ${BPFTOOL_EXECUTABLE}
        gen skeleton ${BPF_PROG_OBJ}
        > ${BPF_SKEL_H}
    DEPENDS ${BPF_PROG_SRC}
    COMMENT "编译 eBPF XDP 程序并生成 libbpf 骨架: xdp_prog.c → xdp_prog.o → xdp_prog.skel.h"
    VERBATIM
)

add_custom_target(bpf_prog DEPENDS ${BPF_PROG_OBJ} ${BPF_SKEL_H})

add_subdirectory(libsentinel)

enable_testing()
add_subdirectory(tests)
