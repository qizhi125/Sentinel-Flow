> **文档状态**: Active
> **最后更新**: 2026-06
> **所属子系统**: Core

# ADR 0002：采用 Share-Nothing Per-CPU 并发模型替代单一 SPSC 管线

## 状态

已采纳（2026-06-21，Phase 0.4）

> **实现状态**：当前代码库（`feat/engine-core-rebuild`）采用**单流水线**简化架构。
> 该 ADR 描述的 Per-CPU 多流水线 + RCU 规则同步属于远期扩展方向，尚未实际实施。
> 当前单流水线设计已满足 Phase 1 吞吐量目标，多核扩展将在性能基准数据驱动下推进。

## 上下文

Phase 0.2 宏观迁移审计（`docs/migration_and_architecture_report.md`）揭示，旧备份 Sentinel-Flow 采用 per-CPU 独立 `PacketPipeline` 实例 + CPU 亲和性绑定（`pthread_setaffinity_np`）的多核并行架构。新工程 `feat/engine-core-rebuild` 分支初期将此简化为单一 SPSC 队列 + 单一消费者线程，导致：
- 多核 CPU 无法被充分利用（单线程吞吐量上限约 500K–1M pps）
- 无 CPU 亲和性设置，NUMA 远端内存访问增加延迟抖动
- 单一 SPSC 队列成为架构瓶颈，无法水平扩展

同时，Sentinel-Flow 的边缘部署目标（工业网关、加固型边缘服务器）通常配备 4–8 核 CPU。不利用多核能力是对硬件资源的浪费。

## 决策

采用 **Share-Nothing Per-CPU 独立管线架构**：

1. **Per-CPU Pipeline 实例**：每个 CPU 核心运行一个独立的 Pipeline 实例，拥有自己的 SPSC 入口队列、PacketParser 状态和 AC 自动机副本。
2. **线程亲和性**：通过 `pthread_setaffinity_np` 将每个 Pipeline 线程绑定到专属 CPU 核心，消除跨核心缓存行弹跳。
3. **内核级 RSS 数据包分发**：基于 **libbpf**/AF_XDP 实现 RSS（Receive Side Scaling），在 XDP BPF 程序中对数据包执行 5 元组哈希，将流量均匀分发到 per-CPU AF_XDP 套接字队列。零软件层面扇出开销。
4. **RCU 全局规则同步**：检测规则作为全局只读状态，通过 RCU（Read-Copy-Update）机制在所有 Pipeline 实例间同步。读路径无锁、无等待（`atomic<shared_ptr<const Matcher>>::load(acquire)`）；写路径构建新自动机 → 原子交换 → 等待读侧临界区退出 → 释放旧自动机。
5. **互斥锁策略**：数据路径 100% 无锁（SPSC + ObjectPool + RCU 读侧）。互斥锁仅限于控制路径（规则加载、配置变更、数据库落盘、关闭流程）。

此决策与 Phase 0.4 重写的 `libsentinel/CLAUDE.md` 中定义的高并发架构原则完全对齐。

## 后果

- **正面**：吞吐量可线性扩展至 CPU 核心数。4 核系统预计实现 2–4× 吞吐量提升
- **正面**：NUMA 感知 — 线程和数据包缓冲区均位于同一 NUMA 域内
- **正面**：零检测盲区 — RCU 规则同步确保读写不互斥
- **负面**：实现复杂度增加 — 需管理多线程生命周期、per-CPU 队列创建、RSS BPF 程序
- **负面**：规则匹配内存占用乘以核心数（每个 Pipeline 持有的 AC 自动机副本）
- **负面**：调试难度提升 — 多线程并发 bug 难以复现和定位
- **行动项**：
  - Phase 2.2：基于 libbpf 实现 AF_XDP + RSS BPF 分发逻辑
  - Phase 1.4：Pipeline 异常恢复机制，单个 Pipeline 线程崩溃不影响其他线程
  - Phase 3.5：当前 `atomic<shared_ptr<Matcher>>` 升级为完整 RCU 模式
  - Phase 4.5：弱内存序平台（ARM/RISC-V）SPSC 正确性验证
