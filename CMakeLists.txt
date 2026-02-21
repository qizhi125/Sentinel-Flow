cmake_minimum_required(VERSION 3.20)
project(Sentinel-Flow VERSION 6.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Charts)

# 查找数据库
find_package(SQLite3 REQUIRED)

# =========================================================
# 🏗️ v6.0 架构头文件包含路径 (Include Paths)
# =========================================================
include_directories(
        src
        src/common/types
        src/common/utils
        src/common/queues
        src/capture/impl
        src/engine/pipeline
        src/engine/flow
        src/engine/context
        src/engine/governance
        src/presentation/views
        src/presentation/views/pages
        src/presentation/views/components
)

# =========================================================
# 📂 自动扫描源文件 (Source Groups)
# =========================================================

# 1. Common Layer
file(GLOB_RECURSE SRC_COMMON
        "src/common/types/*.cpp"
        "src/common/queues/*.cpp"
)

# 2. Capture Layer
file(GLOB_RECURSE SRC_CAPTURE
        "src/capture/impl/*.cpp"
)

# 3. Engine Layer
file(GLOB_RECURSE SRC_ENGINE
        "src/engine/pipeline/*.cpp"
        "src/engine/flow/*.cpp"
        "src/engine/context/*.cpp"
)

# 4. Presentation Layer
file(GLOB_RECURSE SRC_PRESENTATION
        "src/presentation/views/*.cpp"
        "src/presentation/views/pages/*.cpp"
        "src/presentation/views/components/*.cpp"
)

# Main Entry
set(SRC_MAIN "src/main.cpp")

# =========================================================
# 🚀 构建目标
# =========================================================
add_executable(SentinelApp
        ${SRC_MAIN}
        ${SRC_COMMON}
        ${SRC_CAPTURE}
        ${SRC_ENGINE}
        ${SRC_PRESENTATION}
)

# 链接库
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

# 启用 Qt MOC/UIC/RCC 自动处理
set_property(TARGET SentinelApp PROPERTY AUTOMOC ON)
set_property(TARGET SentinelApp PROPERTY AUTOUIC ON)
set_property(TARGET SentinelApp PROPERTY AUTORCC ON)

# 编译选项优化 (开启所有警告，确保高质量)
if(MSVC)
    target_compile_options(SentinelApp PRIVATE /W4)
else()
    target_compile_options(SentinelApp PRIVATE -Wall -Wextra -O2)
endif()