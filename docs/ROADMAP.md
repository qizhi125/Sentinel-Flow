# 项目演进路线图 (Roadmap) - Sentinel-Flow v2.0

本项目采用敏捷迭代与核心架构驱动的开发模式。以下是系统从原型到工业级，再到未来内核旁路架构的演进路线。

##  Phase 1: 骨架奠基与内存安全 (已完成)

- [x] 确立 `capture/`, `engine/`, `presentation/` 严格的单向依赖物理目录结构。
- [x] 重构 `RawPacket`，强制提取 Linux 内核级纳秒时间戳 (`SO_TIMESTAMP`)，确保时间精度。
- [x] **[内存加固]** 彻底修复对象池 (`ObjectPool`) 的析构顺序与内存泄漏，实现物理内存零拷贝 (Zero-Copy)。
- [x] **[环境适配]** 攻克 Linux Sudo/Wayland 环境下的显卡上下文丢失问题，引入 `AA_UseSoftwareOpenGL` 软件渲染防黑屏保护。

## Phase 2: 无锁并发与极速管线 (已完成)

- [x] **[无锁化]** 废弃传统互斥锁，引入基于 C++20 `std::atomic` 的 **单生产者单消费者无锁环形队列 (`SPSCQueue`)**。
- [x] 引入混合退避自旋 (Hybrid Spin Backoff) 策略，解决极高并发下的 Futex 永久死锁与 CPU 空转问题。
- [x] 实施线程的 CPU 亲和性绑定 (Core Affinity)，彻底消除 Cache Bouncing (缓存颠簸) 开销。
- [x] 重构跨线程通信，通过 `qRegisterMetaType` 和 `QSharedPointer` 实现解析结果的批量、零拷贝传递，防止 UI 重绘风暴。

## Phase 3: 工业级引擎与全栈解耦 (当前版本 v2.0)

- [x] **[安全引擎]** 废弃正则匹配，底层重写 **Aho-Corasick (AC) 自动机**，实现针对上万条 IDS 规则的 $O(N)$ 极速载荷扫描。
- [x] **[提权隔离]** 废弃 Root 启动，引入交互式 `setcap cap_net_raw` 与 `execv` 进程热重载，完美遵循最小特权原则。
- [x] **[视图解耦]** 抽离 `PacketDetailRenderer` 与 `UIFactory`，在前后端实现 Wireshark 级别的 L2-L7 层级协议解析树与 Hex 视图。
- [x] **[存储逃逸]** 优化 SQLite WAL 模式，将持久化文件转移至共享特权区，成功绕过 SELinux 强制访问控制引发的只读降级陷阱。
- [x] **[极致净化]** 重构 C++ 链接属性 (`inline`) 与命名空间作用域，达成全工程 0 Error / 0 Warning 的完美编译交付。

---

## Phase 4: 内核旁路与深水区 (规划中)

- [ ] **eBPF / XDP 驱动层**：引入基于 Linux eBPF 的网络驱动引擎，在网卡驱动层（甚至到达网卡硬件前）直接丢弃恶意数据包，绕过整个 Linux 内核网络栈 (Kernel Bypass)。
- [ ] **HTTP/2 & TLS 1.3 探测**：增强 `PacketParser` 的 L7 解析能力，支持 TLS 握手特征 (JA3/JA3S) 提取与 HTTP/2 HPACK 头部解压。
- [ ] **内存映射文件 (mmap) 优化**：针对超大型 `.pcapng` 取证文件，引入 `mmap` 零拷贝读取，进一步压榨磁盘 I/O 极限。

## Phase 5: 云原生与威胁情报 (长期愿景)

- [ ] **分布式架构**：剥离 `DatabaseManager`，支持通过 gRPC 将告警数据上报至远程 Elasticsearch / ClickHouse 集群。
- [ ] **微服务网格 (Service Mesh) 探针**：提供轻量级的无头容器镜像 (Headless Docker Image)，作为 DaemonSet 部署于 Kubernetes 环境，监控 Pod 间的东西向流量。
- [ ] **动态威胁情报 (CTI)**：接入外部 API，支持定时拉取并热更新恶意 IP / 域名黑名单，而无需重启底层流量采集器。