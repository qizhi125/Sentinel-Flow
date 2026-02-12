# Sentinel-Flow v6.0 架构设计文档

## 1. 概述 (Overview)
Sentinel-Flow v6.0 采用了 **"超级交易所 (Hyper-Exchange)" 架构**。该架构灵感来源于高频交易系统 (HFT) 和工业自动化控制系统。旨在以确定性的低延迟处理 10Gbps+ 线速流量，并彻底解决 UI 卡顿问题。

## 2. 核心分层 (Core Layers)

### 第一层：采集与分诊 (Capture & Triage)
- **职责**：原始流量的入口。
- **关键技术**：
    - **内核级打戳 (Kernel Timestamping)**：使用 `SO_TIMESTAMP` 获取纳秒级精度时间，不依赖用户态系统时间。
    - **软 RSS 分流 (Soft-RSS)**：基于五元组 Hash 将流量均衡分发到多个 Worker 队列，保证会话局部性。
    - **零内存分配 (No-Allocation)**：使用预分配的 **对象池 (Object Pool)**，杜绝运行时 `new/delete` 造成的内存碎片。

### 第二层：分析引擎 (Analysis Engine)
- **职责**：分布式业务逻辑处理。
- **模型**：MPMC (多生产者-多消费者) + 工作窃取 (Work Stealing)。
- **流水线级数**：
    - **L1 解码 (Decoder)**：协议头提取 (Ethernet/IP/TCP)。
    - **L2 重组 (Reassembly)**：TCP 流重组与会话状态跟踪。
    - **L3 质检 (Inspector)**：IDS 规则匹配与威胁检测。
- **治理机制**：
    - **优先级队列**：控制报文 (SYN/DNS/ICMP) 走 VIP 通道，不做排队。
    - **智能背压 (Backpressure)**：当队列深度超过 90% 时，主动丢弃低价值的大包 (如视频流)，保全核心业务。

### 第三层：表现层 (Presentation)
- **职责**：数据可视化与用户交互。
- **设计模式**：类 MVVM 模式。
    - **Models (模型)**：负责将后端的高频事件（如 10k PPS）聚合为低频快照（如 60 FPS）。
    - **Views (视图)**：被动渲染，与引擎逻辑彻底解耦。

## 3. 系统架构图
```mermaid
graph TD
    NIC[物理网卡] -->|DMA| CaptureService[采集服务]
    CaptureService -->|Hash 分流| QueueGroup[多核队列组]
    
    subgraph Engine [核心引擎]
        QueueGroup -->|P0 急诊通道| Worker1[分析线程 1]
        QueueGroup -->|P1 普通通道| Worker2[分析线程 2]
        Worker1 -.->|工作窃取| Worker2
    end
    
    Worker1 & Worker2 -->|聚合统计| ViewModel[前端模型]
    ViewModel -->|差异化更新| GUI[主界面]
