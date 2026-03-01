# 演进路线与技术里程碑

本项目采用敏捷迭代开发。系统演进路径围绕吞吐量提升、系统级权限管理以及微服务化展开。

## 运行环境隔离与内存管理 (已完成)

- [x] **架构分层**：确立 `capture/`, `engine/`, `presentation/` 的单向依赖物理目录结构。
- [x] **高精度时钟**：`RawPacket` 集成 Linux 内核级 `SO_TIMESTAMP`，提取纳秒级硬件/内核时间戳。
- [x] **内存生命周期**：修正 `ObjectPool` 析构顺序，结合自定义 Deleter 修复物理内存泄漏，实现数据链路零拷贝。
- [x] **显示服务兼容**：针对 Linux Sudo 提权后在 Wayland/X11 环境下引发的 OpenGL 上下文丢失问题，实装 `AA_UseSoftwareOpenGL` 软渲染后备方案防黑屏。

## 无锁并发与跨线程调度 (已完成)

- [x] **无锁队列**：基于 C++20 `std::atomic` 实现单生产者单消费者环形队列 (`SPSCQueue`)，替代 `std::mutex`。
- [x] **退避策略**：引入 Hybrid Spin Backoff (混合自旋退避) 机制，缓解高并发场景下的 Futex 死锁与 CPU 空转。
- [x] **缓存优化**：通过 `pthread_setaffinity_np` 绑定线程 CPU 核心 (Core Affinity)，消除多核间的 Cache Bouncing。
- [x] **防抖重绘**：通过 `qRegisterMetaType` 与 `QSharedPointer` 跨线程批量投递解析数据，规避 UI 事件循环阻塞。

## DPI 引擎与系统级集成 (当前版本 v2.0)

- [x] **多模式匹配**：重写 Aho-Corasick 自动机替代正则表达式，将包含上万条规则的匹配时间复杂度稳定在 $O(N)$。
- [x] **权限降级**：通过 `socket(AF_PACKET)` 探针结合 `setcap cap_net_raw` 与 `execv` 重载进程，避免以 root 身份直接运行 GUI。
- [x] **视图解耦**：抽离 `PacketDetailRenderer`，独立处理 L2-L7 协议解析树与十六进制 (Hex) 视图渲染。
- [x] **强制访问控制 (MAC) 规避**：调整 SQLite WAL 持久化路径至共享特权目录，绕过 SELinux 导致的数据库只读降级限制。
- [x] **编译期治理**：修复全局 C++ 链接属性 (`inline`) 与命名空间作用域冲突，实现全工程 0 Error / 0 Warning 编译。

## 内核旁路与协议扩展 (规划中)

- [ ] **eBPF / XDP 旁路阻断**：在网卡驱动层注入 eBPF 探针，通过 BPF Map 共享恶意 IP 状态，实现环内核的数据包直接丢弃 (XDP_DROP)。
- [ ] **L7 协议深度解析**：增强 `PacketParser`，支持提取 TLS 1.3 握手特征 (JA3/JA3S) 与 HTTP/2 HPACK 头部解压。
- [ ] **大文件 I/O 优化**：针对 GB 级的 `.pcapng` 离线取证文件，引入 `mmap` (内存映射) 替代标准文件流读取，压榨磁盘 I/O 极限。

## 云原生与威胁情报 (长期愿景)

- [ ] **遥测数据汇聚**：剥离本地 SQLite 依赖，支持通过 gRPC 将告警数据流式输出至远程 Elasticsearch / ClickHouse 集群。
- [ ] **微服务网格探针**：构建剥离 Qt GUI 的 Headless 容器镜像，支持在 Kubernetes 环境下以 DaemonSet 模式部署，监控东西向网络流量。
- [ ] **动态威胁情报 (CTI)**：接入外部 API，实现配置无锁热重载，定时拉取恶意 IP/域名黑名单而不中断数据面管线。




