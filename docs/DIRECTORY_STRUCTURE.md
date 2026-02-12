# 源码目录规范

`src/` 目录是按照 **功能分层** 组织的，而不是按照文件类型。

```text
src/
├── common/                  # [通用层] 共享类型、工具、线程安全原语
│   ├── types/               # 纯数据结构 (RawPacket, Alert)
│   └── queues/              # 队列实现
│
├── capture/                 # [第一层：数据采集]
│   ├── interface/           # 抽象基类 (ICaptureDriver)
│   └── impl/                # 具体实现 (Pcap, eBPF)
│
├── engine/                  # [第二层：核心逻辑与分析]
│   ├── pipeline/            # 调度与编排逻辑
│   ├── workers/             # 实际工作的线程类
│   ├── flow/                # 业务逻辑 (解析, IDS)
│   └── context/             # 全局状态 (规则库, 数据库)
│
└── presentation/            # [第三层：用户界面]
    ├── models/              # ViewModel (负责数据聚合与转换)
    ├── views/               # Qt 窗口与组件
    └── controllers/         # 逻辑控制 (连接 View 与 Model)
```
开发注意 (Rules)

    依赖方向：Presentation -> Engine -> Capture -> Common。严禁反向依赖。

    UI 隔离：engine/ 目录下严禁包含任何 QWidget 或 UI 相关代码。

    接口优先：各层之间应通过 interface/ 目录下的接口进行通信。