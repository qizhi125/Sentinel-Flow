# Sentinel-Flow

基于 **AF_XDP + SPSC 无锁队列 + Aho-Corasick 自动机** 的网络入侵检测系统（NIDS）。C++20 数据面通过 CGO 暴露 C API，Go 控制面提供 CLI 工具与终端仪表盘。

## 架构

```
捕获驱动 (libpcap / AF_XDP)
  → ObjectPool<MemoryBlock> 预分配
    → SPSCQueue<RawPacket> 五元组哈希分发
      → PacketPipeline (per-CPU 线程, pthread 亲和性)
        → PacketParser (L3/L4 + HTTP URI / TLS SNI)
          → SecurityEngine (AhoCorasick, O(N) 多模式匹配)
            → CGO 回调 → Go 告警输出
              → DatabaseManager (SQLite WAL, 批量事务)
```

详细架构规格：[`docs/architecture.md`](./docs/architecture.md)
无锁内存模型：[`docs/lockfree_model.md`](./docs/lockfree_model.md)
C/Go 边界规约：[`docs/cgo_boundary.md`](./docs/cgo_boundary.md)
执行计划：[`ROADMAP.md`](./ROADMAP.md)

## 环境依赖

- **OS**: Linux（推荐 Fedora 42+、Ubuntu 24.04+）
- **编译器**: GCC 14+ 或 Clang 18+（C++20）、Go 1.25+
- **构建**: CMake 3.20+
- **库**: libpcap-devel、sqlite-devel、libbpf-devel（eBPF 可选）、libxdp-devel（可选）

## 构建

```bash
# C++ 静态库
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# 产物: build/libsentinel/libsentinel_core.a

# Go CLI
go build -o sentinel-cli ./cmd/sentinel
```

构建指南详情：[`docs/setup.md`](./docs/setup.md)

## 运行

```bash
# 授予网络权限
sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli

# 启动
./sentinel-cli -i eth0 -r ./configs/rules.yaml -w 4
```

## CLI 参数

```
sentinel-cli [flags]

  -i        string   网络接口名称 (默认: lo)
  -r        string   YAML 规则文件路径 (默认: ./configs/rules.yaml)
  -w        int      工作线程数 1-64 (默认: 4)
  --ebpf    bool     启用 AF_XDP 零拷贝捕获 (默认: false)
  --offline string   离线 PCAP 文件路径 (默认: "" 即在线模式)
  -v        bool     启用详细日志 (默认: false)
  -h        bool     显示帮助
```

## 规则配置

`configs/rules.yaml`:

```yaml
rules:
  - id: 1001
    enabled: true
    protocol: "ANY"
    pattern: "attack_pattern"
    level: 4
    description: "检测到攻击载荷"
```

Go 侧通过 `fsnotify` 监听文件变更，500ms 防抖后自动热重载 AC 自动机。

## 测试

```bash
# C++ 单元测试
cmake --build build --target sentinel_tests -j$(nproc)
cd build && ctest --output-on-failure

# Go 测试
go test ./pkg/engine/ -v
```

## 目录结构

```
├── cmd/sentinel/              # Go CLI 入口
├── pkg/engine/                # CGO 绑定层 + 终端仪表盘
├── libsentinel/               # C++20 核心数据面
│   ├── include/sentinel/      # 对外 C API 头文件 (capi.h)
│   └── src/
│       ├── capi_impl.cpp      # C API 实现 + EngineContext 生命周期
│       ├── capture/
│       │   ├── interface/     # ICaptureDriver 抽象接口
│       │   ├── impl/          # PcapCapture (libpcap)
│       │   └── driver/        # EBPFCapture (AF_XDP) + xdp_prog.c
│       ├── common/
│       │   ├── memory/        # ObjectPool (Tagged Pointer 无锁)
│       │   ├── queues/        # SPSCQueue (无锁环形队列)
│       │   ├── types/         # NetworkTypes, GlobalStats
│       │   └── utils/         # Logger, StringUtils
│       └── engine/
│           ├── context/       # DatabaseManager (SQLite WAL)
│           ├── flow/          # AhoCorasick, PacketParser, SecurityEngine
│           ├── interface/     # IInspector
│           ├── pipeline/      # PacketPipeline (per-CPU 线程)
│           ├── governance/    # AuditLogger
│           └── workers/       # ForensicWorker, WorkerBase
├── docs/                      # 架构 + 边界规约 + 构建/运维指南
├── ROADMAP.md                 # 三阶段执行计划
├── configs/rules.yaml         # 示例规则配置
├── tests/                     # C++ 单元测试 (GTest)
└── third_party/googletest/
```

## 许可证

MIT License
