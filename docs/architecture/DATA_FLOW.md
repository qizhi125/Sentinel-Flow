# 数据生命周期与内存拓扑

本文档追踪网络报文在 Sentinel-Flow 系统中的完整生命周期。系统采用单向数据流设计，通过智能指针托管底层内存池 (`ObjectPool`) 的内存块，确保数据包载荷在跨线程与跨模块传递时实现“零拷贝”。

## 入站与内存获取

1. **链路层探测**：`PcapCapture` 线程获取底层驱动的数据链路类型。若为 `DLT_LINUX_SLL` (Linux Cooked Capture)，设置 `linkOffset = 16`；否则按标准以太网帧设置为 `14`。
2. **网卡捕获与打戳**：通过 `pcap_next_ex` 读取报文，直接提取 `pcap_pkthdr` 中的 `ts` 字段，将其换算为纳秒级精度的时间戳 (`kernelTimestampNs`)。
3. **无锁借用**：调用 `PacketPool::instance().acquire()` 从无锁内存池中弹出一个空闲的 `MemoryBlock`。
4. **浅拷贝装载**：将原始字节流通过 `memcpy` 复制到该区块中，并使用带有自定义析构器 (`BlockDeleter`) 的智能指针封装为 `RawPacket` 结构。

## 分流与跨线程调度

1. **软隔离分发 (Soft-RSS)**：计算报文 L3 协议头的源与目的 IP 异或哈希值 (`saddr ^ daddr % queueCount`)，锁定特定的 Worker 队列。
2. **防拥塞截断 (Backpressure)**：
    - 检查目标 `SPSCQueue` 积压是否超过 5000 帧。
    - 若发生拥塞，不再拷贝完整 Payload，强制将拷贝长度截断为 `linkOffset + 20 + 20 + 64` (即保留 L2/L3/L4 头部及 64 字节余量)。
    - 设置 `raw.isTruncated = true` 标志位，确保高压下不丢失核心会话元数据。
3. **原子入队**：将 `RawPacket` 压入目标 Worker 专属的单生产者单消费者队列 (`SPSCQueue`)。内存所有权正式由捕获线程转移至队列。

## 业务解析与特征检测

1. **核绑定出队**：绑定了专属物理核心的 `PacketPipeline` 线程，通过混合自旋退避策略调用 `popWait(100ms)` 从队列中原子提取 `RawPacket`。
2. **偏移量解剖**：`PacketParser` 按 L2 -> L3 -> L4 顺序移动指针偏移量，提取字段并构造出 `ParsedPacket` 结构。
3. **黑名单短路拦截 (Early Drop)**：查询 `SecurityEngine`，若 `ParsedPacket` 的源 IP 或目的 IP 命中全局黑名单，立刻终止该报文生命周期（`continue` 丢弃），释放内存，不进入后续计算密集型管线。
4. **安全引擎扫描**：未被拦截的载荷进入 `SecurityEngine`，利用预构建的 Aho-Corasick 自动机以 $O(N)$ 的时间复杂度完成多模式并发扫描。

## 批处理与控制面分发

完成检测后，释放原始的 `MemoryBlock` 强引用（`parsed.block.reset()`，生命周期交由外层接管），数据进入两条独立分支：

- **告警落盘分支**：命中规则的报文生成 `Alert` 实体，压入 `DatabaseManager` 的条件变量队列（上限 20,000 条，超限直接丢弃防 OOM）。后台线程以 1000 条为单位，通过 `BEGIN IMMEDIATE/COMMIT` 聚合写入 SQLite WAL 数据库。
- **视图渲染分支**：`PacketPipeline` 将 `ParsedPacket` 暂存至本地 `QVector`。当积压达到 5000 条或时间跨度超过 150ms 时，生成 `QSharedPointer<QVector>` 通过 Qt 事件循环跨线程投递至 UI 主线程。

## 淘汰与内存回收

1. **模型装载**：主线程的 `TrafficTableModel` 接收批处理信号，调用 `beginInsertRows` 将数据追加至内部的 `std::deque<ParsedPacket>` 结构中。
2. **容量控制**：当内部队列长度突破 100,000 行阈值时，调用 `beginRemoveRows` 和 `pop_front()` 淘汰最老的一批历史记录。
3. **无锁回收**：随着最老的 `ParsedPacket` 被析构，其持有的所有底层结构的生命周期终结，触发无锁内存池的回收机制。区块的内存地址通过 `compare_exchange_weak` 原语重新压回 `ObjectPool` 的空闲链表头部 (Tagged Head)，完成绝对的零分配轮回。


