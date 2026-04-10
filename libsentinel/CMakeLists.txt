cmake_minimum_required(VERSION 3.20)
project(libsentinel)

# 1. 查找必要的依赖
find_package(PCAP REQUIRED)

# 2. 定义核心 C++ 源码
set(CORE_SOURCES
        src/capi_impl.cpp
        src/common/memory/ObjectPool.h
        src/common/queues/SPSCQueue.h
        src/capture/impl/PcapCapture.cpp
        src/capture/driver/EBPFCapture.cpp
        src/engine/context/DatabaseManager.cpp
        src/engine/flow/PacketParser.cpp
        src/engine/flow/SecurityEngine.cpp
        src/engine/pipeline/PacketPipeline.cpp
)

add_library(sentinel_core STATIC ${CORE_SOURCES})

target_include_directories(sentinel_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(sentinel_core PUBLIC pcap sqlite3 stdc++)

# eBPF 探针自动编译逻辑
# 查找 clang 编译器 (eBPF 必须使用 LLVM/Clang)
find_program(CLANG_EXE clang)

if(CLANG_EXE)
    message(STATUS "🚀 发现 Clang: ${CLANG_EXE}，已启用 eBPF 自动编译")

    # 定义输出的 BPF 字节码路径 (放在构建目录下)
    set(BPF_OBJ_OUT ${CMAKE_CURRENT_BINARY_DIR}/xdp_prog.o)

    # 添加自定义编译命令
    add_custom_command(
            OUTPUT ${BPF_OBJ_OUT}
            COMMAND ${CLANG_EXE} -O2 -target bpf -g -c ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/driver/xdp_prog.c -o ${BPF_OBJ_OUT}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/capture/driver/xdp_prog.c
            COMMENT "Compiling eBPF program: xdp_prog.c -> xdp_prog.o"
            VERBATIM
    )

    add_custom_target(bpf_prog ALL DEPENDS ${BPF_OBJ_OUT})

    # 强制要求在编译 sentinel_core 之前，先编译出 BPF 字节码
    add_dependencies(sentinel_core bpf_prog)
else()
    message(WARNING "⚠️ 未找到 clang 编译器，eBPF 探针 (xdp_prog.c) 将不会被编译！请安装 clang。")
endif()