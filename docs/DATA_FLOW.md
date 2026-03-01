# 数据生命周期与内存拓扑 (Data Lifecycle & Memory Topology) - Sentinel-Flow v2.0

本文档描述了一个网络数据包在 Sentinel-Flow v2.0 (Hyper-Exchange 架构) 中，从物理网卡被捕获，直至最终在前端屏幕上渲染的“极速漂流”。

全链路严格遵循 **零拷贝 (Zero-Copy)** 和 **无锁并发 (Lock-Free)** 原则。数据包的物理载荷在内存中自始至终只存在一份，跨线程转移的仅仅是带有自定义回收逻辑的智能指针。

---

## 第一阶段：入站与内存出借 (Ingress & Memory Borrowing)

1. **网卡抵达 (NIC Rx)**：数据包到达物理网卡的接收队列，触发软中断。
2. **内核打戳 (Kernel Timestamping)**：Linux 内核网络栈通过 `SO_TIMESTAMP` 为其打上纳秒级精度的硬件/内核时间戳，确保后续取证的绝对时间精度。
3. **特权捕获 (Raw Socket)**：以 `cap_net_raw` 特权启动的 `PcapCapture` 线程通过底层的内存映射截获该报文。
4. **无锁借用 (Pool Pop)**：引擎直接向预热好的无锁对象池 (`ObjectPool`) 发起原子 `pop`，获取一个空闲的 `MemoryBlock`。
5. **浅拷贝装载 (Shallow Copy)**：将网卡中的原始字节流 `memcpy` 至该 `MemoryBlock` 中，并包装为带有自定义析构器 (`Deleter`) 的 `std::shared_ptr`，构成 `RawPacket` 结构。

## 第二阶段：分流与无锁调度 (Dispatch & Lock-Free Queueing)

1. **软分流 (Soft-RSS)**：解析器快速提取报文的五元组（源/目的 IP、源/目的端口、协议），计算其 Hash 值。
2. **亲和性映射 (Core Affinity)**：通过 Hash 值取模，将属于同一个 TCP 会话的所有报文，严格映射到同一个逻辑 `WorkerID`，保证 L4 会话状态机的局部性 (Locality)。
3. **原子入队 (Atomic Push)**：`PcapCapture` 线程将 `RawPacket` 指针推入目标 Worker 专属的 **单生产者单消费者无锁环形队列 (`SPSCQueue`)** 中，内存所有权 (Ownership) 正式从捕获线程转移至队列。

## ⚙️ 第三阶段：解析与深度检测 (Parsing & DPI)

1. **混合自旋出队 (Hybrid-Spin Pop)**：独占物理核心的 `PacketPipeline` 工作线程，通过混合自旋策略（无锁等待）从 `SPSCQueue` 中弹出数据包。
2. **剥离报头 (Header Stripping)**：`PacketParser` 按 L2 (Ethernet) -> L3 (IPv4) -> L4 (TCP/UDP) 的顺序移动指针偏移量，提取出结构化的 `ParsedPacket` 元数据对象。
3. **O(N) 自动机检测 (AC Automaton)**：载荷指针被直接送入 `SecurityEngine`。引擎利用预先编译好的 **Aho-Corasick 状态机**，对 Payload 进行 $O(N)$ 时间复杂度的并发扫描，瞬间判定是否命中上万条入侵检测 (IDS) 规则或黑名单。

## 第四阶段：分发与控制面转移 (Divergence & Control Plane)

经过检测的 `ParsedPacket` 将在这里走向两条并行的支线：

- **支线 A：告警与取证落盘 (Threat Storage)**
  - 如果触发了 Critical / High 级别的安全告警，数据包的元数据将被打包发送给 `AuditLogger`，异步写入 **SQLite WAL 数据库**。
  - `ForensicManager` 介入，直接从内存池中提取原始载荷，落盘生成标准的 `.pcap` 证据文件供第三方溯源。

- **支线 B：前端视图渲染 (UI Rendering)**
  - 数据包无法逐个发送给前端（会导致 UI 事件循环崩溃）。`PacketPipeline` 将解析后的数据包缓冲进一个本地的 `std::vector`。
  - 每达到 5000 个包或时间阈值，打包为一个 `QVector<ParsedPacket>`，通过 Qt 的信号槽机制 (`Qt::QueuedConnection`) 跨线程投递给主线程的 `TrafficTableModel`。

## 第五阶段：销毁与内存归还 (Destruction & Memory Return)

1. **视图消费 (View Consumption)**：Qt 主线程的 `TrafficMonitorPage` 接收到批处理数据，将其挂载到虚拟列表模型中进行增量重绘。
2. **生命周期终结 (Ref-Count Zero)**：当数据包滚出 UI 的历史限制（或用户清空列表），`TrafficTableModel` 中的 `std::shared_ptr` 引用计数降为 0。
3. **无锁归还 (Pool Push)**：智能指针的自定义析构器被触发，它不会调用危险的 `delete`，而是直接将底层的 `MemoryBlock` 通过无锁原子操作重新压回 `ObjectPool`。
4. **轮回 (Rebirth)**：内存块回到第一阶段，等待承载下一个万兆洪峰中的数据包。