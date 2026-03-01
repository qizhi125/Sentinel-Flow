# eBPF 捕获策略

## 概述

当前 Sentinel-Flow 使用 `libpcap` 作为默认捕获后端，虽然性能已通过无锁队列、内存池等优化大幅提升，但在万兆甚至更高吞吐场景下，内核态到用户态的数据拷贝、系统调用开销仍可能成为瓶颈。eBPF (Extended Berkeley Packet Filter) 作为 Linux 内核的先进技术，允许在网卡驱动层或内核协议栈中注入自定义程序，实现**零拷贝**、**内核态过滤**和**直接丢弃**，是下一代高性能网络监控的首选方案。

本文档描述了 eBPF 捕获策略的设计目标、技术方案、与现有架构的集成路径以及未来规划。

## 设计目标

- **极致性能**：在 10Gbps 甚至更高带宽下实现零丢包捕获。
- **内核态过滤**：在网卡驱动层直接丢弃无关流量，减少用户态数据拷贝。
- **动态加载**：无需重启进程即可更新 BPF 程序或过滤规则。
- **与现有架构无缝集成**：通过 `ICaptureDriver` 接口统一，可动态切换 `PcapCapture` 与 `EBPFCapture`。

## 技术方案

### 1. XDP (eXpress Data Path)

XDP 是 Linux 内核提供的高性能、可编程的数据路径，在网卡驱动收到数据包后、内核协议栈处理前执行 eBPF 程序。

**优势**：
- 最早介入点，性能最优。
- 支持 `XDP_DROP` 直接丢弃，`XDP_TX` 转发，`XDP_PASS` 传递给协议栈。
- 可通过 AF_XDP 实现零拷贝用户态接收。

**集成方式**：
- 编写 eBPF C 程序，编译为 BPF 字节码。
- 使用 `libbpf` 或 `bpftool` 加载到网卡。
- 通过 BPF map 与用户态共享统计信息、黑名单等。

### 2. AF_XDP (Address Family XDP)

AF_XDP 允许用户态程序通过 socket 直接从 XDP 程序接收数据包，无需经过协议栈，实现真正的零拷贝。

**优势**：
- 绕过内核协议栈，大幅降低延迟。
- 支持多队列，可绑定 CPU 核心。
- 与 `libpcap` 的 AF_XDP 后端兼容。

**集成方式**：
- 创建 AF_XDP socket，绑定到指定网卡和队列。
- 通过 `recvmsg` 或 `poll` 接收数据包。
- 数据包直接写入用户态预分配的内存区域（`umem`），零拷贝。

### 3. BPF 过滤器动态更新

- 使用 BPF map 存储过滤规则（如 IP 黑名单），XDP 程序读取 map 决定是否丢弃。
- 用户态可通过 `bpf_map_update_elem` 动态更新黑名单，无需重启。
- 支持复杂的 BPF 表达式（如 BPF 字节码），但推荐使用简单规则以保持高性能。

## 与现有架构的集成

### 驱动接口统一

`ICaptureDriver` 定义了捕获驱动的统一接口：

```cpp
    class ICaptureDriver {
    public:
        virtual void init(const std::vector<PacketQueue*>& queues) = 0;
        virtual void start(const std::string& device) = 0;
        virtual void stop() = 0;
        virtual bool setFilter(const std::string& filterExp) = 0;
        virtual std::vector<std::string> getDeviceList() = 0;
    };
```

`EBPFCapture` 将实现此接口，与 `PcapCapture` 并列，通过配置或命令行参数选择后端。

### 数据流适配

- **初始化**：加载 eBPF 程序到指定网卡，创建 AF_XDP socket，绑定到多个队列（按 CPU 核心数）。
- **接收循环**：通过 `poll` 监听 socket，接收数据包，复制到 `MemoryBlock`，计算哈希后推入 SPSC 队列。
- **过滤**：XDP 程序直接根据 BPF map 中的黑名单丢弃报文，`setFilter` 用于更新 map。

### 内存管理

- 使用 `PacketPool` 管理用户态内存，与 `PcapCapture` 一致。
- AF_XDP 的 `umem` 需预分配连续大页内存，与 `ObjectPool` 的分配方式不同，需做适配或单独管理。

## 性能预期

| 指标 | libpcap (现有) | eBPF/XDP (规划) |
|------|----------------|-----------------|
| 最大吞吐 | 约 2-4 Gbps (单核) | 10 Gbps+ (单核) |
| 丢包率 | 高负载下可能丢包 | 极低，接近线速 |
| CPU 占用 | 较高 (系统调用、数据拷贝) | 较低 (内核态处理、零拷贝) |
| 过滤效率 | BPF 在内核执行，但仍有拷贝 | XDP 直接丢弃，零拷贝 |

## 实施计划

### 阶段一：原型验证 (1-2 月)

- 编写简单 XDP 程序，实现包计数和基本过滤。
- 集成 AF_XDP 接收路径，验证零拷贝可行性。
- 适配 `PacketPool` 与 AF_XDP `umem` 内存管理。

### 阶段二：驱动集成 (2-3 月)

- 实现 `EBPFCapture` 类，满足 `ICaptureDriver` 接口。
- 支持动态加载 BPF 程序，与现有解析管线对接。
- 实现黑名单 BPF map 动态更新。

### 阶段三：优化与生产就绪 (3-4 月)

- 支持多队列，自动绑定 CPU 核心。
- 实现热切换机制，无需重启即可在后端间切换。
- 添加性能监控指标（如 `bpftool` 输出）。

## 使用示例

```bash
    # 启动时指定使用 eBPF 后端（占位）
    ./SentinelApp --gui --driver=ebpf
    
    # 或通过配置文件切换
    # config.ini
    # capture_driver=ebpf
```

## 注意事项

- eBPF 需要 Linux 内核版本 >= 4.15，推荐 5.4+。
- XDP 需要网卡驱动支持（大多数主流网卡已支持）。
- 需安装 `libbpf`、`bpftool` 等开发工具。
- 若 eBPF 不可用，系统自动回退到 `libpcap` 模式。

## 参考资料

- [BPF and XDP Reference Guide](https://docs.cilium.io/en/stable/bpf/)
- [AF_XDP Kernel Documentation](https://www.kernel.org/doc/html/latest/networking/af_xdp.html)
- [libbpf GitHub](https://github.com/libbpf/libbpf)

---
