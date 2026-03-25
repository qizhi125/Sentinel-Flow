# 项目演进路线图 (Roadmap)

## Phase 1: 骨架与重构 (已完成)
- [x] 实施 v1.0 功能分层目录结构 (`capture/`, `engine/`, `presentation/`)。
- [x] 修复 CMake 构建系统与 Qt6 依赖链。
- [x] 改造 `RawPacket` 强制携带 Linux 内核级纳秒时间戳 (`SO_TIMESTAMP`)。
- [x] **[安全加固]** 修复对象池 (`ObjectPool`) 的析构与内存泄漏问题。
- [x] **[安全加固]** 修复 UI 挂载生命周期：纠正 `setupUi` 提前调用导致的空指针静默炸弹。
- [x] **[环境适配]** 攻克 Linux Sudo/Wayland 环境下的显卡上下文丢失问题，引入 `AA_UseSoftwareOpenGL` 软件渲染防黑屏保护。

## Phase 2: 无锁并发与管线改造 (已完成)
- [x] 废弃传统互斥锁，引入基于 `std::atomic` 的 **单生产者单消费者无锁环形队列 (`SPSCQueue`)**。
- [x] 废弃 `std::vector<ThreadSafeQueue>` 方案，根据 CPU 物理核心数自动分配 `PacketPipeline` Worker 池。
- [x] 实施线程的 CPU 亲和性绑定 (Core Affinity)，消除 CPU 缓存一致性 (Cache Bouncing) 开销。
- [x] 重构跨线程通信，通过 `qRegisterMetaType` 和 `QSharedPointer` 智能指针实现解析结果的**批量、零拷贝传递**。

## Phase 3: 工业级性能与 UI 极速渲染 (已完成)
- [x] 安全引擎内核大换血，集成 **Aho-Corasick (AC) 自动机**，实现 O(N) 时间复杂度的多模式工业级扫描。
- [x] 优化并发瓶颈，将全局黑名单控制锁升级为 `std::shared_mutex` (读写锁)。
- [x] 引入独立的高危事件异步取证系统 (`ForensicWorker`)，防止 PCAP I/O 落盘阻塞高速解析管线。
- [x] 彻底摒弃卡顿的 `QTableWidget`，重构前端数据引擎，引入基于 `QAbstractTableModel` 的 **虚拟列表 (`TrafficTableModel`)**，轻松支撑 10万+ 行数据无阻塞渲染。
- [x] 增强 SQLite 数据库事务健壮性，使用 `BEGIN IMMEDIATE` 防止死锁并引入 WAL 模式。

## Phase 4: 协议深解析与智能化扩展 (当前与未来焦点)
- [ ] **[当前焦点]** 协议深度解析引擎 (DPI)：支持从 L4 深入至 HTTP/DNS/TLS 等应用层协议特征提取。
- [ ] 支持安全规则与黑名单的动态热加载 (无需停止抓包即可替换 AC 自动机规则树)。
- [ ] 实施 "智能分诊与背压"：在系统内存拥塞或队列超载时，主动丢弃低价值大包 (如视频流) 以保全核心控制流。
- [ ] 实现 Linux eBPF 高性能捕获驱动，替代 Libpcap，实现真正的内核旁路 (Kernel Bypass) 零拷贝。
- [ ] 提供 Lua 脚本接口用于编写自定义的协议解析插件与封堵规则。