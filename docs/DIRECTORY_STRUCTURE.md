# 源码目录规范 (Directory Structure)

`src/` 目录严格按照 **功能分层 (Layered Architecture)** 组织，确保数据流向清晰、模块高度解耦。当前的 v6.0 架构全面拥抱了无锁并发与 MVC 视图分离。

```text
src/
├── common/                  # [通用层] 共享类型、工具、无锁并发原语
│   ├── memory/              # 内存管理 (ObjectPool 无锁对象池)
│   ├── queues/              # 队列机制 (SPSCQueue 无锁单产单消队列, ThreadSafeQueue)
│   ├── types/               # 纯数据结构 (RawPacket, ParsedPacket, Alert 等)
│   └── utils/               # 公共工具类 (StringUtils 等)
│
├── capture/                 # [第一层：捕获层] 负责从底层网卡获取原始数据报文
│   ├── driver/              # 底层高性能驱动预留 (如未来的 EBPFCapture)
│   ├── impl/                # 捕获接口的具体实现 (PcapCapture)
│   └── interface/           # 捕获层抽象基类 (ICaptureDriver)
│
├── engine/                  # [第二层：引擎层] 协议解析、安全检测与核心调度逻辑
│   ├── context/             # 全局状态与持久化 (DatabaseManager, ForensicManager)
│   ├── flow/                # 业务流与深度检测 (PacketParser, SecurityEngine, AhoCorasick)
│   ├── governance/          # 系统治理与监控 (AuditLogger)
│   ├── interface/           # 引擎标准化接口 (IInspector)
│   ├── pipeline/            # 核心绑定的高性能调度管线 (PacketPipeline)
│   └── workers/             # 异步后台辅助任务 (ForensicWorker)
│
└── presentation/            # [第三层：表现层] 基于 Qt6 的 MVC 可视化交互界面
    ├── controllers/         # 控制器层：处理复杂的视图间业务逻辑转移 (MainController)
    ├── models/              # 模型层：负责后台高频数据的聚合与降频抽象 (TrafficModel)
    └── views/               # 视图层：具体的 UI 窗体与绘图组件
        ├── components/      # 高复用性的独立可视化组件 (TrafficTableModel 虚拟列表等)
        ├── pages/           # 按业务切分的独立子页面 (DashboardPage, TrafficMonitorPage 等)
        ├── styles/          # 动态深色模式控制器与 QSS 样式表资源 (ThemeManager)
        └── MainWindow.cpp/.h # 全局主窗口，负责路由、布局挂载与页面生命周期管理

```

## 开发准则 (Master Rules)

1. **依赖单向流动 (Unidirectional Dependency)** `presentation` -> `engine` -> `capture` -> `common`。严禁发生反向调用或环形依赖。表现层必须通过接口或跨线程信号 (`QSharedPointer`) 被动接收引擎数据。
2. **UI 物理隔离 (UI Isolation)** `engine/` 和 `capture/` 目录下严禁 `#include` 任何 `<QWidget>`、`<QChart>` 或 GUI 相关的 Qt 模块。引擎层只负责处理 `std::vector` 和原生数据类型。
3. **内存热路径禁区 (Hot-Path Strictness)** 在 `capture/impl` 和 `engine/pipeline` 的核心数据流循环中，严禁使用 `new/delete`、`std::string` 的动态拼接，以及任何会导致线程阻塞的同步原语（如 `std::mutex`、I/O 系统调用）。必须绝对依赖 `ObjectPool` 和 `SPSCQueue`。

