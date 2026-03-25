# 数据生命周期 (Data Lifecycle)

本文档描述了一个数据包在 Sentinel-Flow v6.0 (Hyper-Exchange 架构) 中，从网卡捕获到屏幕渲染的“极速漂流”。全链路严格遵循**零拷贝 (Zero-copy)** 和 **无锁并发 (Lock-free)** 原则。

## 第一阶段：入站 (内核态 -> 用户态)
1.  **抵达**：数据包到达物理网卡的接收队列 (Rx Queue)。
2.  **打戳**：Linux 内核通过 `SO_TIMESTAMP` 打上纳秒级时间戳 (`kernelTimestampNs`)。
3.  **借用内存**：`PcapCapture` 驱动直接从 **无锁对象池 (ObjectPool)** 中弹出一个空闲的 `RawPacket` 内存块，避免任何 `new` 操作。
4.  **软分流 (Soft-RSS)**：计算五元组的 Hash 值，确定该数据包应归属的逻辑 `WorkerID`，保证同一 TCP 会话的局部性。
5.  **无锁入队**：数据包指针被推入对应物理核心的 **单生产者单消费者无锁队列 (`SPSCQueue`)**。

## 第二阶段：处理 (核心解析流水线)
1.  **原子出队**：绑定了独立物理核心的 `PacketPipeline` 线程通过 `popWait` 原子操作获取数据包，全程零互斥锁开销。
2.  **协议解析**：`PacketParser` 层层剥离 Ethernet / IPv4 / TCP / UDP 头部，提取关键元数据。
3.  **O(N) 深度检测**：`SecurityEngine` 接入数据，底层通过 **Aho-Corasick (AC) 自动机** 对 Payload 进行多模式并发扫描。无论规则库有 10 条还是 10000 条，扫描时间始终为 $O(N)$。
4.  **异步取证**：若 AC 自动机命中高危规则，引擎立刻将数据包引用丢给 `ForensicWorker` 进行后台异步 PCAP 落盘，绝不阻塞当前核心的解析进度。
5.  **组装**：提取显示所需的必要字段，生成轻量级的 `ParsedPacket` 结构体。

## 第三阶段：出站 (批量聚合 -> 虚拟视图)
1.  **聚合与释放**：`PacketPipeline` 将生成的 `ParsedPacket` 塞入线程局部的 `QVector` 缓冲区；同时，原始的 `RawPacket` 内存块被**立即归还**给对象池循环利用。
2.  **跨线程零拷贝**：当缓冲区满或定时器触发时，将 `QVector` 包装进 `QSharedPointer`。通过 Qt 的 `qRegisterMetaType`，安全地跨线程投递给主 UI 线程，避免深拷贝。
3.  **模型更新 (MVC)**：主界面的 `TrafficTableModel` 接收批量数据，将其追加到底层的 `std::deque` 缓冲池中，并精确触发 `beginInsertRows` 信号。
4.  **按需重绘**：前端 `QTableView` 作为虚拟列表，**仅计算并重绘当前屏幕可见的几十行数据**。即便是 10 万+ 行的高频插入，UI 也能保持 60 FPS 的极致顺滑。

##  核心基石：时间戳的绝对真实性
我们 **绝不** 使用 `QDateTime::currentDateTime()` 来显示数据包到达时间。
无论流水线由于流量突发产生了多少毫秒的排队延迟，我们始终透传内核在硬件中断时打上的 `kernelTimestampNs`。**这是确保网络取证具备法律效力的绝对底线。**