cmake_minimum_required(VERSION 3.20)
project(libsentinel)

option(BUILD_EBPF "Build eBPF XDP program and driver" ON)
include(CheckIncludeFile)

# 查找必要的依赖
find_package(PCAP REQUIRED)

# 定义核心 C++ 源码
set(CORE_SOURCES
        src/capi_impl.cpp
        src/common/memory/ObjectPool.h
        src/common/queues/SPSCQueue.h
        src/capture/impl/PcapCapture.cpp
        src/engine/context/DatabaseManager.cpp
        src/engine/flow/PacketParser.cpp
        src/engine/flow/SecurityEngine.cpp
        src/engine/pipeline/PacketPipeline.cpp
)

# 条件性添加 eBPF 驱动源文件
if(BUILD_EBPF)
    list(APPEND CORE_SOURCES src/capture/driver/EBPFCapture.cpp)
    message(STATUS "eBPF driver source included")
else()
    message(STATUS "eBPF driver source excluded (BUILD_EBPF=OFF)")
endif()

add_library(sentinel_core STATIC ${CORE_SOURCES})

target_include_directories(sentinel_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sentinel_core PUBLIC pcap sqlite3 stdc++)

if(BUILD_EBPF)
    target_link_libraries(sentinel_core PUBLIC xdp bpf)
endif()

# eBPF 探针自动编译逻辑
find_program(CLANG_EXE clang)

if(CLANG_EXE AND BUILD_EBPF)
    message(STATUS "Found Clang: ${CLANG_EXE}, eBPF auto-compilation enabled")

    set(BPF_OBJ_OUT ${CMAKE_CURRENT_BINARY_DIR}/xdp_prog.o)
    set(BPF_MULTIARCH_INCLUDE "")
    set(BPF_MULTIARCH_DIR "/usr/include/${CMAKE_LIBRARY_ARCHITECTURE}")

    if(CMAKE_LIBRARY_ARCHITECTURE AND EXISTS "${BPF_MULTIARCH_DIR}")
        set(BPF_MULTIARCH_INCLUDE "-I${BPF_MULTIARCH_DIR}")
        message(STATUS "Detected multiarch include dir: ${BPF_MULTIARCH_DIR}")
    endif()

    check_include_file("asm/types.h" HAVE_ASM_TYPES_H)
    if(NOT HAVE_ASM_TYPES_H)
        message(FATAL_ERROR
                "Missing kernel UAPI header asm/types.h. "
                "Please install linux-libc-dev (Debian/Ubuntu) or the equivalent kernel headers package.")
    endif()

    add_custom_command(
            OUTPUT ${BPF_OBJ_OUT}
            COMMAND ${CLANG_EXE} -O2 -target bpf -g -c
                    ${BPF_MULTIARCH_INCLUDE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/driver/xdp_prog.c
                    -o ${BPF_OBJ_OUT}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/driver/xdp_prog.c
            COMMENT "Compiling eBPF program: xdp_prog.c -> xdp_prog.o"
            VERBATIM
    )

    add_custom_target(bpf_prog ALL DEPENDS ${BPF_OBJ_OUT})
    add_dependencies(sentinel_core bpf_prog)
elseif(BUILD_EBPF)
    message(WARNING "Clang not found, eBPF program compilation skipped")
else()
    message(STATUS "eBPF compilation skipped (BUILD_EBPF=OFF)")
endif()