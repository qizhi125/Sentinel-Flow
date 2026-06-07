# 捕获子系统

## 1. PCAP 驱动实现

### 概述

`PcapCapture` 是 Sentinel-Flow 默认的流量捕获驱动，基于 `libpcap` 库实现。它负责从物理网卡或离线文件获取原始网络报文，通过哈希分流将报文分发至多个无锁队列，并实现智能背压保护机制。

### 类设计

```cpp
class PcapCapture : public sentinel::capture::ICaptureDriver {
public:
    static PcapCapture& instance();              // 单例访问

    void init(const std::vector<PacketQueue*>& queues) override;
    void start(const std::string& device) override;
    void stop() override;
    bool setFilter(const std::string& filterExp) override;
    std::vector<std::string> getDeviceList() override;

private:
    void captureLoop();                           // 捕获主循环
    int hashPacket(const uint8_t* data, int len, uint32_t offset);

    std::atomic<bool> running{false};
    std::thread captureThread;
    std::string currentDevice;

    std::vector<PacketQueue*> workerQueues;
    size_t queueCount = 0;

    mutable std::shared_mutex handleMutex;        // 保护 pcap_t 句柄
};
```

### 初始化与启动

1. **初始化**：`init()` 接收工作队列指针列表，保存到 `workerQueues`。
2. **启动**：`start(device)` 设置设备名，启动独立线程 `captureLoop`。

### 捕获循环核心流程

```cpp
void PcapCapture::captureLoop() {
    // 1. 打开设备
    handle = pcap_open_live(currentDevice.c_str(), BUFSIZ, 1, 1000, errbuf);
    // 2. 确定链路层偏移
    int dlt = pcap_datalink(handle);
    uint32_t linkOffset = (dlt == DLT_LINUX_SLL) ? 16 : 14;

    while (running) {
        // 3. 获取下一个报文
        int res = pcap_next_ex(handle, &header, &pkt_data);
        // 4. 哈希分流
        int workerId = hashPacket(pkt_data, caplen, linkOffset);
        auto* targetQueue = workerQueues[workerId];

        // 5. 背压控制
        bool isCongested = (targetQueue->size() > 5000);
        uint32_t copyLen = isCongested ? (linkOffset + 20 + 20 + 64) : caplen;

        // 6. 从内存池获取内存块
        MemoryBlock* rawBlock = PacketPool::instance().acquire();
        std::memcpy(rawBlock->data, pkt_data, copyLen);
        rawBlock->size = copyLen;

        // 7. 构造 RawPacket
        RawPacket raw;
        raw.kernelTimestampNs = header->ts.tv_sec * 1e9 + header->ts.tv_usec * 1000;
        raw.linkLayerOffset = linkOffset;
        raw.block = BlockPtr(rawBlock, BlockDeleter());
        raw.isTruncated = isCongested;

        // 8. 推入队列
        targetQueue->push(std::move(raw));
    }
}
```

### 哈希分流算法

```cpp
int PcapCapture::hashPacket(const uint8_t* data, int len, uint32_t offset) {
    if (len >= static_cast<int>(offset + 20)) {
        uint32_t saddr = *(uint32_t*)(data + offset + 12);  // 源 IP
        uint32_t daddr = *(uint32_t*)(data + offset + 16);  // 目的 IP
        return (saddr ^ daddr) % queueCount;                // 异或取模
    }
    return 0;
}
```

- 使用源 IP 与目的 IP 的异或结果进行取模，保证同一会话的报文进入同一队列。
- 若报文长度不足以提取 IP 头部（如 ARP），则默认送入队列 0。

### 背压截断机制

当目标队列积压超过 **5000** 个报文时，触发截断：
- 仅拷贝头部数据（链路层 + IP 头 + TCP/UDP 头 + 64 字节余量）。
- 设置 `isTruncated = true` 标志，解析器将据此标记载荷被截断。
- 此机制防止内存池耗尽，并确保关键元数据（五元组、标志位）不丢失。

### BPF 过滤器

```cpp
bool PcapCapture::setFilter(const std::string& filterExp) {
    std::unique_lock lock(handleMutex);
    if (!handle) return false;

    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filterExp.c_str(), 1, PCAP_NETMASK_UNKNOWN) == -1)
        return false;

    bool success = (pcap_setfilter(handle, &fp) != -1);
    pcap_freecode(&fp);
    return success;
}
```

- 通过 `pcap_compile` 编译 BPF 表达式，然后调用 `pcap_setfilter` 应用到当前会话。
- 使用 `std::shared_mutex` 保护句柄，支持运行时动态修改过滤器。
- 过滤器表达式在**内核层面**丢弃无关报文，极大减少用户态数据拷贝。

### 线程安全

- `handleMutex` 用于保护 `pcap_t` 句柄，确保在设置过滤器或停止捕获时不会并发访问。
- 捕获线程与停止线程通过原子变量 `running` 同步，使用 `pcap_breakloop` 中断阻塞的 `pcap_next_ex`。

### 性能建议

- **队列数量**：建议与 CPU 核心数匹配，通常设置为 `core_count - 2`。
- **混杂模式**：通过 `pcap_open_live` 的第三个参数启用（`1` 表示混杂模式）。
- **超时设置**：1000ms 的读取超时可保证在低流量时仍能及时退出。
- **BPF 优化**：尽量在过滤器中使用简单表达式，避免复杂逻辑影响内核性能。

---

## 2. eBPF 捕获策略

### 概述

eBPF (Extended Berkeley Packet Filter) 作为 Linux 内核的先进技术，允许在网卡驱动层或内核协议栈中注入自定义程序，实现**零拷贝**、**内核态过滤**和**直接丢弃**，是下一代高性能网络监控的首选方案。

### 设计目标

- **极致性能**：在 10Gbps 甚至更高带宽下实现零丢包捕获。
- **内核态过滤**：在网卡驱动层直接丢弃无关流量，减少用户态数据拷贝。
- **动态加载**：无需重启进程即可更新 BPF 程序或过滤规则。
- **与现有架构无缝集成**：通过 `ICaptureDriver` 接口统一，可动态切换 `PcapCapture` 与 `EBPFCapture`。

### 技术方案

#### XDP (eXpress Data Path)

XDP 是 Linux 内核提供的高性能、可编程的数据路径，在网卡驱动收到数据包后、内核协议栈处理前执行 eBPF 程序。

**优势**：
- 最早介入点，性能最优。
- 支持 `XDP_DROP` 直接丢弃，`XDP_TX` 转发，`XDP_PASS` 传递给协议栈。
- 可通过 AF_XDP 实现零拷贝用户态接收。

#### AF_XDP (Address Family XDP)

AF_XDP 允许用户态程序通过 socket 直接从 XDP 程序接收数据包，无需经过协议栈，实现真正的零拷贝。

**优势**：
- 绕过内核协议栈，大幅降低延迟。
- 支持多队列，可绑定 CPU 核心。
- 与 `libpcap` 的 AF_XDP 后端兼容。

#### BPF 过滤器动态更新

- 使用 BPF map 存储过滤规则（如 IP 黑名单），XDP 程序读取 map 决定是否丢弃。
- 用户态可通过 `bpf_map_update_elem` 动态更新黑名单，无需重启。

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

`EBPFCapture` 已实现此接口，与 `PcapCapture` 并列。通过命令行参数选择后端：

```bash
# 启用 eBPF 模式（需提前编译并加载 xdp_prog.o）
./bin/sentinel-cli -i eth0 --ebpf
```

### 性能预期

| 指标     | libpcap (现有)             | eBPF/XDP (规划)           |
| -------- | -------------------------- | ------------------------- |
| 最大吞吐 | 约 2-4 Gbps (单核)         | 10 Gbps+ (单核)           |
| 丢包率   | 高负载下可能丢包           | 极低，接近线速            |
| CPU 占用 | 较高 (系统调用、数据拷贝)  | 较低 (内核态处理、零拷贝) |
| 过滤效率 | BPF 在内核执行，但仍有拷贝 | XDP 直接丢弃，零拷贝      |

### 实施计划

**阶段一：基础集成（已完成）**
- ✅ 实现 `EBPFCapture` 类，满足 `ICaptureDriver` 接口。
- ✅ 支持加载外部 XDP 程序 `xdp_prog.o`，创建 AF_XDP socket。
- ✅ 与现有解析管线对接，通过 SPSC 队列分发报文。

**阶段二：优化与完善（进行中）**
- [ ] 将 `xdp_prog.c` 编译集成至 CMake 构建流程。
- [ ] 支持多队列自动绑定 CPU 核心。
- [ ] 实现 BPF map 动态更新黑名单的 API。
- [ ] 添加 eBPF 运行状态监控指标（如 XDP 丢包计数）。

**阶段三：生产就绪**
- [ ] 实现运行时后端热切换，无需重启进程即可在 pcap/eBPF 间切换。
- [ ] 提供 XDP 程序版本管理与签名验证。
- [ ] 完善性能调优文档与故障排查指南。

### 注意事项

- eBPF 需要 Linux 内核版本 >= 4.15，推荐 5.4+。
- XDP 需要网卡驱动支持（大多数主流网卡已支持）。
- 需安装 `libbpf`、`libxdp` 开发包。
- 若未授予 `CAP_NET_ADMIN` 和 `CAP_BPF` 能力，eBPF 模式将无法启动。

---

## 3. 零拷贝内存池

### 概述

`ObjectPool` 是 Sentinel-Flow 实现零拷贝数据流的核心基础组件。它管理固定大小的 `MemoryBlock` 对象，提供无锁的分配与回收机制，确保数据包在捕获、解析、消费全链路中避免频繁的 `new`/`delete` 操作，从而消除内存碎片并提升性能。

### 设计目标

- **热路径零分配**：在数据包处理的核心循环中，禁止动态内存分配，所有内存从预分配池中借用。
- **无锁并发**：使用原子操作实现空闲链表，支持多线程安全地获取和释放对象。
- **内存局部性**：所有对象预分配在连续内存区域，提高缓存命中率。
- **生命周期托管**：通过自定义删除器与 `std::shared_ptr` 结合，自动归还内存到池中。

### 核心数据结构

#### MemoryBlock

```cpp
struct MemoryBlock {
    uint8_t data[MAX_PACKET_SIZE];   // 固定大小缓冲区 (2048 字节)
    uint32_t size = 0;               // 实际数据长度
};
```

- `MAX_PACKET_SIZE = 2048` 可覆盖绝大多数网络报文（含以太网帧头）。
- 每个 `MemoryBlock` 由 `ObjectPool<MemoryBlock>` 管理。

### 无锁空闲链表

空闲链表使用 **Tagged Pointer** 技术解决 ABA 问题：

```cpp
struct TaggedHead {
    PoolBlock* ptr = nullptr;
    std::uint64_t tag = 0;   // 版本计数器
};
std::atomic<TaggedHead> freeHead_;
```

- 每次 CAS 操作时，同时比较指针和 tag。
- 若指针相同但 tag 不同，操作失败，防止 ABA 问题。

### 预分配与动态扩容

```cpp
ObjectPool(std::size_t preallocate) {
    reserve(preallocate);
}
```

- 构造函数支持预分配数量，减少运行时分配。
- 若池中无可用对象，`acquire()` 会动态分配新节点（使用互斥锁保护全局分配列表）。

### 生命周期管理

- **获取**：优先从空闲链表获取，若为空则分配新节点。
- **释放**：通过 `fromObjectPtr` 获取所属节点，放回空闲链表。对象本身不销毁，仅放回池中。
- **智能指针包装**：提供 `UniquePtr` 类型，自动调用 `release`。

### 在数据流中的使用

1. **捕获阶段**：从 `PacketPool` 获取 `MemoryBlock`，填充数据后推入 SPSC 队列。
2. **解析阶段**：`ParsedPacket` 保留 `block` 成员，与 `RawPacket` 共享所有权。
3. **消费与释放**：`parsed.block.reset()` 显式释放，或随 `ParsedPacket` 析构自动释放。

### 性能特性

- **无锁操作**：`acquire`/`release` 在有空闲节点时完全无锁。
- **缓存友好**：节点预分配在连续内存，链表操作只在头部进行。
- **零拷贝**：数据块从捕获到消费始终是同一块物理内存。

### 配置建议

- 默认池容量：20000 个 `MemoryBlock`（约 40 MB 内存）。
- 可根据网络吞吐量调整预分配数量：`PacketPool::instance().reserve(50000);`。
