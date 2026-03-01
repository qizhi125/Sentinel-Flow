# 源码目录规范 (Directory Structure) - Sentinel-Flow v2.0

`src/` 目录严格按照 **功能分层 (Layered Architecture)** 组织，确保数据流向清晰、模块高度解耦。当前的 v2.0 架构全面拥抱了无锁并发与 MVC/组件化视图分离。

---

## 🌳 完整目录树 (The Tree)

```text
src/
├── common/                  # [基础层] 共享类型、工具与无锁并发原语
│   ├── memory/              # 内存管理 (ObjectPool 无锁零拷贝对象池)
│   ├── queues/              # 队列机制 (SPSCQueue 无锁单产单消环形队列)
│   ├── types/               # 纯数据结构 (RawPacket, ParsedPacket, Alert 告警实体)
│   └── utils/               # 公共静态工具类 (StringUtils 字符串与字节格式化)
│
├── capture/                 # [捕获层] 负责从操作系统底层/网卡获取原始报文
│   ├── driver/              # 底层高性能驱动预留接口 (如未来的 EBPFCapture)
│   ├── impl/                # 捕获接口的具体实现 (PcapCapture pcap引擎封装)
│   └── interface/           # 捕获层抽象基类 (ICaptureDriver 依赖倒置接口)
│
├── engine/                  # [引擎层] 协议解析、安全检测与核心后台调度逻辑
│   ├── context/             # 全局状态与持久化 (DatabaseManager SQLite封装, ForensicManager)
│   ├── flow/                # 业务流与检测 (PacketParser 协议解剖, SecurityEngine, AhoCorasick 自动机)
│   ├── governance/          # 系统治理与审计 (AuditLogger)
│   ├── interface/           # 引擎标准化抽象接口 (IInspector)
│   ├── pipeline/            # 核心绑定的高性能调度管线 (PacketPipeline 无锁消费者)
│   └── workers/             # 异步后台辅助任务线程 (ForensicWorker 异步落盘取证)
│
├── main.cpp                 # [入口点] 唯一的 C++ 程序入口
│
└── presentation/            # [表现层] 基于 Qt6 的可视化 GUI 与终端 CLI
    ├── app/                 # 核心启动器与系统环境交互 (SentinelLauncher 提权重载器)
    ├── cli/                 # 无头终端守护模式 (CliEngineManager)
    ├── controllers/         # 控制器层：处理跨视图间复杂业务流转 (MainController)
    ├── models/              # 模型层：负责后台高频数据的聚合与降频抽象 (TrafficModel)
    └── views/               # 视图层：具体的 UI 窗体与绘图组件
        ├── components/      # [高复用组件库] (PacketDetailRenderer 协议树, UIFactory 工厂, TrafficTableModel)
        ├── pages/           # 按业务切分的独立主页面 (DashboardPage, ForensicPage, TrafficMonitorPage)
        │   └── rules/       # 规则页面的子 Tab 控制 (IdsRulesTab)
        ├── styles/          # 动态主题控制器与界面资源配置 (ThemeManager, global.h)
        └── MainWindow.cpp/h # 全局主窗口，负责路由、布局挂载与生命周期管理
```

## 架构开发准则 (Architecture Master Rules)

任何人向本仓库提交代码，必须严格遵守以下物理隔离原则：

1. **依赖单向流动 (Unidirectional Dependency)**
    - `presentation` 可以依赖 `engine` 和 `common`。
    - `engine` 可以依赖 `capture` 和 `common`。
    - `engine` 或 `capture` 层中绝不允许出现任何 `#include <QWidget>` 或 `presentation/` 目录下的头文件。底层对前端必须是完全无感知的。
2. **视图层组件化 (UI Componentization - DRY 原则)**
    - 任何在两个以上的 Page 视图中出现的相似 UI 元素（如信息提示框、十六进制字节树），必须提取到 `presentation/views/components/` 目录下（如 `UIFactory` 或 `PacketDetailRenderer`）。
    - 禁止在 `XXXPage.cpp` 中堆砌冗余的通用计算逻辑。
3. **零分配热路径 (Zero-Allocation on Hot Path)**
    - 在 `capture/` 和 `engine/pipeline/` 的数据包接收和解析主循环中，严禁使用 `new` 或 `malloc`。所有内存必须向 `common/memory/ObjectPool` 借用。
4. **锁的静默化 (Mutex Silence)**
    - `engine/pipeline` 和 `capture/impl` 的跨线程通讯必须通过 `common/queues/SPSCQueue` 完成。在万兆流量的数据面（Data Plane）上，严禁引入 `std::mutex`。