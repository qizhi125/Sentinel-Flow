# 源码与工程目录规范

本项目代码结构严格遵循**领域驱动与模块隔离**原则。`src/` 目录下的物理隔离直接映射了系统的逻辑架构边界，所有代码提交必须符合本文档定义的单向依赖准则。

## 完整目录树

```text
Sentinel-Flow/
├── docs/                              # [文档工程] 领域驱动的架构规范书
│   ├── architecture/                  # 核心架构拓扑与数据流生命周期
│   ├── capture/                       # 底层网卡与内存池设计机制
│   ├── engine/                        # IDS 匹配引擎与管线细节
│   ├── operations/                    # 运维指南与性能调优
│   ├── presentation/                  # 前端渲染与 CLI 守护进程
│   ├── storage/                       # 持久化与取证文件生成
│   ├── DIRECTORY_STRUCTURE.md         # 本文件
│   └── ROADMAP.md                     # 演进路线图
│
├── src/                               # 源代码根目录
│   ├── main.cpp                       # 唯一入口点，调用 SentinelLauncher
│   │
│   ├── common/                        # [基础层] 跨模块共享的基础设施
│   │   ├── memory/                    # 无锁内存池
│   │   │   └── ObjectPool.h
│   │   ├── queues/                    # 无锁队列
│   │   │   └── SPSCQueue.h
│   │   ├── types/                     # 全局数据结构定义
│   │   │   └── NetworkTypes.h         # RawPacket, ParsedPacket, Alert, PacketPool
│   │   └── utils/                     # 工具函数集合
│   │       └── StringUtils.h          # 字符串处理、十六进制转储、格式化
│   │
│   ├── capture/                       # [捕获层] 数据面起点
│   │   ├── driver/                    # 驱动接口定义与预留实现
│   │   │   └── EBPFCapture.h          # eBPF 驱动占位
│   │   ├── impl/                      # 实际捕获引擎
│   │   │   ├── PcapCapture.cpp
│   │   │   └── PcapCapture.h
│   │   └── interface/                 # 统一捕获接口
│   │       └── ICaptureDriver.h
│   │
│   ├── engine/                        # [引擎层] 核心检测与解析逻辑
│   │   ├── context/                   # 持久化上下文
│   │   │   ├── DatabaseManager.cpp
│   │   │   └── DatabaseManager.h
│   │   ├── flow/                      # 协议解析与安全检测
│   │   │   ├── AhoCorasick.h
│   │   │   ├── PacketParser.cpp
│   │   │   ├── PacketParser.h
│   │   │   ├── SecurityEngine.cpp
│   │   │   └── SecurityEngine.h
│   │   ├── governance/                # 审计与日志
│   │   │   └── AuditLogger.h
│   │   ├── interface/                 # 检测接口定义
│   │   │   └── IInspector.h
│   │   ├── pipeline/                  # 解析流水线
│   │   │   ├── PacketPipeline.cpp
│   │   │   └── PacketPipeline.h
│   │   └── workers/                   # 后台工作线程
│   │       ├── ForensicWorker.h       # 取证文件写入
│   │       └── WorkerBase.h           # 工作线程基类
│   │
│   └── presentation/                  # [表现层] 控制面与 UI 渲染
│       ├── app/                       # 应用程序入口与权限提升
│       │   ├── SentinelLauncher.cpp
│       │   └── SentinelLauncher.h
│       ├── cli/                       # 终端守护进程 (ncurses / ANSI)
│       │   ├── CliEngineManager.cpp
│       │   └── CliEngineManager.h
│       ├── controllers/               # 控制器（预留）
│       │   └── MainController.h
│       ├── models/                    # 数据模型
│       │   └── TrafficModel.h
│       └── views/                     # 视图组件
│           ├── components/            # 可复用 UI 组件
│           │   ├── BpfHighlighter.cpp
│           │   ├── BpfHighlighter.h
│           │   ├── PacketDetailRenderer.cpp
│           │   ├── PacketDetailRenderer.h
│           │   ├── StatCard.cpp
│           │   ├── StatCard.h
│           │   ├── TrafficTableModel.cpp
│           │   ├── TrafficTableModel.h
│           │   ├── TrafficWaveChart.cpp
│           │   ├── TrafficWaveChart.h
│           │   ├── UIFactory.cpp
│           │   └── UIFactory.h
│           ├── pages/                 # 主界面各页面
│           │   ├── AlertsPage.cpp
│           │   ├── AlertsPage.h
│           │   ├── DashboardPage.cpp
│           │   ├── DashboardPage.h
│           │   ├── ForensicPage.cpp
│           │   ├── ForensicPage.h
│           │   ├── RulesPage.cpp
│           │   ├── RulesPage.h
│           │   ├── SettingsPage.cpp
│           │   ├── SettingsPage.h
│           │   ├── StatisticsPage.cpp
│           │   ├── StatisticsPage.h
│           │   ├── TrafficMonitorPage.cpp
│           │   ├── TrafficMonitorPage.h
│           │   └── rules/            # 规则管理子页面
│           │       ├── IdsRulesTab.cpp
│           │       └── IdsRulesTab.h
│           ├── styles/                # 主题与样式定义
│           │   ├── global.h
│           │   ├── StatisticsStyle.h
│           │   ├── ThemeDefinitions.h
│           │   ├── ThemeManager.h
│           │   └── TrafficMonitorStyle.h
│           ├── MainWindow.cpp
│           └── MainWindow.h
│
└── tests/                             # 单元测试与基准测试用例 (待完善)
    └── CMakeLists.txt                 # 测试构建文件
```

## 架构开发准则

任何对 `src/` 的代码合并请求 (PR)，必须接受以下四项物理隔离约束的 Code Review：

### 1. 严格的单向依赖

- **准则**：`presentation` → `engine` → `capture` / `common`。
- **红线**：底层模块绝对不可感知上层模块。在 `engine/` 或 `capture/` 层中，严禁出现 `#include <QWidget>` 或任何 `presentation/` 目录下的头文件。

### 2. 热路径零分配

- **准则**：在 `capture/` 和 `engine/pipeline/` 的数据包接收和解析主循环（数据面）中，所有内存必须通过 `PacketPool::instance().acquire()` 借用。
- **红线**：严禁在万兆处理管线中直接调用 `new`、`malloc` 或引发 `std::vector` 的深拷贝扩容，违者将导致严重的系统级中断和 OOM。

### 3. 数据面锁静默

- **准则**：跨线程的报文流转必须且只能通过 `common/queues/SPSCQueue` 完成。
- **红线**：在处理实际网络流量的 Worker 线程中，严禁引入 `std::mutex`、`std::shared_mutex` 或任何会触发内核态 Futex 阻塞的同步原语。仅允许使用 C++20 `std::atomic` 配合确定的内存序进行自旋同步。

### 4. 视图组件 DRY 原则

- **准则**：所有在业务主窗体 (Pages) 中复用的 L2-L7 协议树、Hex 视图或统计图表，必须下沉抽象至 `presentation/views/components/`，通过工厂或静态渲染器（如 `PacketDetailRenderer`）统一调用。
```

---

更新说明：
1. 补充了 `common/utils/`、`engine/governance/`、`engine/interface/`、`engine/workers/` 等目录。
2. 完善了 `presentation/` 下所有子目录，包括 `cli/`、`controllers/`、`models/`、`views/` 的详细文件。
3. 保持原有“架构开发准则”不变，并增加一项“视图组件 DRY 原则”作为第四条。
4. 调整了目录树的缩进和注释，使之与实际结构一致。