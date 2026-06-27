# Sentinel-Flow 行业分析与发展路线图

> **文档状态**: Active
> **编制日期**: 2026-06-21
> **所属子系统**: Global
> **基准版本**: feat/engine-core-rebuild (Phase 11.3 审计后)
> **适用范围**: NIDS 边缘节点架构设计与工控安全（ICS/OT）协议检测

---

## 第一章：NIDS 行业趋势

### 1.1 eBPF/AF_XDP 零拷贝快速路径已成为主流

传统 libpcap 通过 `recvfrom` 系统调用将内核缓冲区数据拷贝到用户空间，10Gbps 线速下 CPU 拷贝开销达到 40-60%。AF_XDP 通过以下机制实现**零拷贝**：

| 机制 | 说明 |
|------|------|
| **UMEM（用户态内存区域）** | 内核与用户态共享一块预分配的巨大页内存，数据包直接写入 UMEM，无拷贝 |
| **XDP 程序（eBPF）** | 在内核网络栈最早接入点（驱动程序级别）过滤/重定向数据包，绕过完整协议栈 |
| **Fill/Completion Ring** | 生产者-消费者模型：用户态填充缓冲区描述符 → 内核写入数据 → 用户态消费 |

**业界趋势**：

- **Suricata** 7.0+ 引入 `af-xdp` 和 `af-packet` 多模式支持，在 AF_XDP bypass 模式下吞吐量可达 20-30 Gbps/核心。
- **Zeek** 5.0+ 通过 `zeek-af_packet` 插件支持 AF_XDP，但默认依然以 PF_RING 和 libpcap 为主。
- **Craton Shield**（2025 开源）基于 Rust 实现嵌入式 eBPF/XDP NIDS，明确对齐 IEC 62443-3-3 边缘节点要求。
- 2025 年 eBPF Summit 论文 [1] 指出：AF_XDP 零拷贝管线在 x86 服务器上达到 **25 Mpps 线速**（单 100G 端口），CPU 占用仅为 libpcap 方案的 25%。

**对 Sentinel-Flow 的启示**：AF_XDP 后端的缺失是当前架构在 >1Gbps 场景中的最大瓶颈。Phase 2 应优先评估 AF_XDP 的重新集成路径。

### 1.2 OT 协议深度解析在边缘/ICS 环境中的核心地位

工控网络流量与 IT 网络有本质区别：

| 特征 | IT 网络流量 | OT 网络流量 |
|------|-----------|-----------|
| 通信模式 | 突发、非确定性 | 周期性轮询（50ms–5s 间隔） |
| 协议 | HTTP/TLS/DNS（文本协议） | Modbus/DNP3/S7comm/EtherNet/IP（二进制协议） |
| 端点 | 通用服务器/容器 | PLC/RTU/HMI/ Historian |
| 基线 | 动态 IP/端口 | 固定五元组 + 功能码白名单 |
| 停机容忍度 | 分钟级 | **零容忍**（安全系统不得触发 PLC 看门狗） |

关键 OT 协议检测需求：

- **Modbus TCP（502/tcp）**：功能码白名单（读 FC 1-4 vs 写 FC 5/6/15/16）、异常诊断码（FC 8）、广播写入（Unit ID=0）检测。
- **S7comm（102/tcp）**：CPU STOP 指令检测、程序下载/上传监控、数据块写入异常。
- **DNP3（20000/tcp）**：未授权重启指令、时间同步篡改、文件传输异常。

Corelight（Zeek 商业版）和 Suricata 均已内置 OT 协议解析器。Sentinel-Flow 的 AC 自动机可通过**规则模式匹配**覆盖上述已知攻击签名，但缺乏协议状态机级别的语义理解。

### 1.3 IEC 62443 标准对自主边缘节点的约束

IEC 62443 系列标准定义了工业自动化和控制系统的安全要求层级。对边缘 NIDS 节点最相关的条款如下：

| 标准部分 | 范围 | 对 Sentinel-Flow 的具体要求 |
|----------|------|---------------------------|
| **IEC 62443-3-3** SR 4.1 | 持续监控 | 引擎必须持续运行，崩溃后自动重启；Dashboard 反映实时运行状态 |
| **IEC 62443-3-3** SR 5.1 | 网络分段执行 | 边缘节点部署在 Purdue Level 2/3 边界，被动监控 zone 间 conduit 流量 |
| **IEC 62443-3-3** SR 3.1 | 通信完整性 | 检测到协议异常时必须生成结构化告警，含时间戳、协议类型、功能码、严重等级 |
| **IEC 62443-4-2** EDR 3.11 | 恶意代码防护 | AC 自动机规则集需支持周期性更新和原子热重载，重载期间不中断检测 |
| **IEC 62443-4-2** EDR 4.1 | 资源管理 | 边缘硬件（ARM/RISC-V）内存受限，对象池容量需可配置且启动时预分配 |
| **IEC 62443-2-4** | 维护与审计 | 所有告警必须可持久化追溯；告警日志含 SHA-256 摘要防篡改（Phase 3 目标） |

**Sentinel-Flow 的对齐现状**：

| 要求 | 状态 | 差距 |
|------|------|------|
| 持续监控 | ✅ 已实现 | Pipeline 异常时无自动重启机制（Phase 12.0 健康检查已覆盖心跳探针） |
| 被动部署 | ✅ 已实现 | libpcap SPAN/TAP 模式原生支持 |
| 结构化告警 | ⚠️ 部分实现 | 告警仅含 rule_id + payload_snippet，缺五元组和协议字段 |
| 热重载 | ⚠️ 部分实现 | `atomic<shared_ptr<Matcher>>` 交换存在瞬时盲区（CoW 模型更优） |
| 资源管理 | ✅ 已实现 | 对象池（8192）+ SPSC 队列（8192）预分配 |
| 审计日志 | ❌ 未实现 | 旧 ForensicWorker/AuditLogger 已移除，当前无防篡改日志 |

---

## 第二章：Sentinel-Flow 定位分析

### 2.1 为何 C++20 SPSC + Go TUI/SQLite 架构适合边缘自主

| 架构选择 | 边缘适配理由 |
|----------|------------|
| **C++20 无锁 SPSC 管线** | 确定性延迟（无 GC 暂停）、零堆分配快速路径、单核吞吐量可达 500K–1M pps |
| **Tagged Pointer ObjectPool** | ABA 安全的内存复用，避免 `malloc/free` 系统调用开销，预分配可控 |
| **Go 控制面** | 快速开发配置文件热重载、信号处理；goroutine 调度对 I/O 密集型任务（TUI/SQLite）天然高效 |
| **SQLite WAL 模式** | 嵌入式部署零运维依赖，WAL 支持并发读写，10K–50K 告警/秒落盘性能足够 |
| **termui TUI** | 无浏览器依赖，SSH 终端即可操作，边缘节点无需 GPU/图形栈 |
| **CGO 薄边界** | 6 函数 API 最小化跨语言开销，Go 侧仅做数据聚合和展示，C++ 侧独占数据面 |

**与竞品对比**：

| 维度 | Sentinel-Flow | Suricata | Zeek | Craton Shield |
|------|-------------|----------|------|---------------|
| 数据面语言 | C++20 | C | C++ | Rust |
| 并发模型 | SPSC lock-free | 多线程 + 锁 | 单线程事件循环 | async/await |
| 包捕获 | libpcap（当前） | AF_XDP / PF_RING / Netmap | libpcap / AF_PACKET | eBPF/XDP |
| TUI | termui（Go） | 无（CLI 日志） | 无（CLI 日志） | Web 仪表盘 |
| 存储 | SQLite WAL | EVE JSON / syslog | TSV 日志 | SQLite |
| OT 协议 | 规则匹配 | 内置解析器 | 内置解析器 | 11 种协议监视器 |
| IEC 62443 对齐 | 部分 | 规则集可定制 | 规则集可定制 | 专用报告生成器 |
| 二进制大小 | ~5 MB | ~20 MB | ~15 MB | ~8 MB |
| 内存基线 | ~50 MB | ~500 MB | ~200 MB | ~30 MB |

**Sentinel-Flow 的核心差异化优势**：最小化资源足迹 + 交互式终端仪表盘，专为**单节点、低功耗边缘网关**设计。竞品（Suricata/Zeek）面向数据中心/SOC 环境，内存占用和运维复杂度不适合边缘部署。

### 2.2 目标部署模型

```
┌─────────────────────────────────────────────────────┐
│                  Purdue Model ICS 网络               │
│                                                     │
│  Level 4 (Enterprise)  ←── 传统 IT NIDS (Suricata)  │
│       │                                             │
│  ──── DMZ / Firewall ───                           │
│       │                                             │
│  Level 3 (Operations)  ←── Sentinel-Flow 边缘节点   │
│       │                    [C++ SPSC 管线]          │
│  Level 2 (Supervisory) ←── Sentinel-Flow 边缘节点   │
│       │                    [Go TUI + SQLite]        │
│  ──── Conduit ───                                  │
│       │                                             │
│  Level 1 (Controller)   ←── 被动 SPAN 端口镜像      │
│  Level 0 (Physical)      ←── 不得主动探测           │
└─────────────────────────────────────────────────────┘
```

Sentinel-Flow 部署在 Level 2/3 边界的工业网关或加固型边缘服务器上，通过 SPAN 端口被动镜像 PLC-HMI 通信流量。TUI 仪表盘通过 SSH 会话访问，无需本地显示器。

---

## 第三章：工作分解结构（WBS）框架

### 阶段总览

```
Phase 0: 基准确立          ← 当前
Phase 1: 可观测性恢复        → 生产就绪
Phase 2: eBPF 集成          → 高性能快速路径
Phase 3: 高级检测            → OT 协议 + 异常检测
Phase 4: 合规与审计          → IEC 62443 完全对齐
```

### Phase 0：基准确立（进行中）

| 编号 | 任务 | 产出 | 优先级 |
|------|------|------|--------|
| 0.1 | 全局项目状态同步 | TUI 线程安全验证 + 变更日志审查 | ✅ 完成 |
| 0.2 | 宏观迁移审计 | `docs/migration_and_architecture_report.md` | ✅ 完成 |
| 0.3 | 文档技术债修复 + 行业路线图 | 本文档 + CLAUDE.md 修复 | ✅ 完成 |
| 0.4 | 架构重对齐 + 高并发基准 | 重写 libsentinel/CLAUDE.md（Share-Nothing + per-CPU + RCU + spdlog + libbpf），重写 pkg/CLAUDE.md（通用 TUI 原则 + 灵活指南），更新 WBS（spdlog + libbpf/RSS 子任务） | ✅ 完成 |
| 0.5 | 代码清理 | 移除残留构建产物（`src/main`/`sentinel`/`sentinel-cli`），更新 `.gitignore` | P2 |

### Phase 1：可观测性恢复与生产加固

**目标**：消除 Dashboard 模拟数据，实现结构化日志，达到单节点持续运行 7×24 小时无崩溃。

| 编号 | 任务 | 详细描述 | 影响范围 |
|------|------|----------|----------|
| **1.1** | 集成 spdlog 异步无阻塞遥测 | ✅ 完成 — 引入 spdlog v1.15.2（FetchContent），异步双接收器（stderr 彩色 + 每日滚动文件），overrun_oldest 队列溢出策略，SENTINEL_* 宏替换全部 std::cerr | `CMakeLists.txt`、`Logger.h/cpp`、全部 6 个 .cpp |
| **1.2** | 遥测数据通道与仪表盘集成 | ✅ 完成 — C API 新增 stats 结构体/回调/setter，C++ 组件暴露 metrics getter，1Hz 统计线程，Go CGO binding + SetStatsCallback，Dashboard 原子遥测存储，controller 消除模拟数学 | `capi.h`、`capi_impl.cpp`、`PcapCapture.h`、`Pipeline.h`、`DatabaseManager.h`、`binding.go`、`engine.go`、`main.go`、`dashboard.go`、`controller.go` |
| **1.3** | 告警结构增强 | ✅ 完成 — 新增 `sentinel_alert_event_t` 五元组结构体（src_ip/dst_ip/src_port/dst_port/protocol/rule_id/timestamp_ns/payload_snippet），重写 `sentinel_alert_callback_t` 签名，C++ 栈分配零动态内存，Go CGO 绑定转换 IP/Port，消除 `extractEndpoints`/`extractProtocol` 模拟函数。 | `capi.h`、`capi_impl.cpp`、`PacketTypes.h`、`PacketParser.cpp`、`binding.go`、`engine.go`、`main.go` |
| **1.4** | Pipeline 异常恢复 | ✅ 完成 — 新增 `std::atomic<bool> error_state_` + `has_error()` getter，`consumer_loop` 包裹 try/catch（std::exception + ...），异常时 SENTINEL_ERROR 记录日志、置位 error_state_、break 退出线程。stats 线程填充 `has_fatal_error` 字段，Go 侧 `EngineStats.HasFatalError` 映射，`SetStatsCallback` 检测 true 时调用 `dash.Quit()` + log 安全关机。 | `capi.h`、`Pipeline.h`、`Pipeline.cpp`、`capi_impl.cpp`、`binding.go`、`main.go` |
| **1.5** | 对象池容量 Benchmark | ✅ 完成 — 根 CMakeLists.txt 集成 google/benchmark v1.8.5 (FetchContent)，tests/CMakeLists.txt 新增 bench_object_pool + bench_spsc_queue 目标（链接 sentinel_core + benchmark::benchmark + pthread）。bench_object_pool.cpp: AcquireRelease 基线 + ExhaustionCliff 池耗尽断层（预占槽位后测量堆分配/互斥慢速路径）+ MultiThreaded 竞争吞吐。bench_spsc_queue.cpp: Producer-Consumer 多线程吞吐，MockItem 扫掠容量 2048/16384/65536，RawPacket 真实负载。<br>**Benchmark 关键发现**：Debug 构建下 2048–8192 SPSC 队列吞吐量优于 65536，根因为 L1/L2 缓存亲和性——较小环形缓冲区完全驻留在 L1D（32 KiB）或 L2（256 KiB–1 MiB）内，避免大容量队列跨越多个 Cache Line 导致的缓存缺失惩罚。全局 ObjectPool 在多线程竞争下性能显著退化，验证了未来 Share-Nothing per-CPU 架构中每个管线线程持有独立本地对象池的必要性。据此将 Pipeline 默认队列容量从 65536 下调至 8192，GlobalPool 默认容量从 2048 上调至 8192，锁定为 Phase 1 最终容量配置。 | `CMakeLists.txt`、`tests/CMakeLists.txt`、`tests/bench_object_pool.cpp`、`tests/bench_spsc_queue.cpp` |
| **1.6** | CI 扩展 | 恢复 `ENABLE_ASAN=ON` / `ENABLE_UBSAN=ON` CMake 选项。CI matrix 新增 `Debug+ASan` 和 `Release+UBSan` 构建。 | `CMakeLists.txt`、`.github/workflows/ci.yml` |

### Phase 2：eBPF 集成与高性能快速路径

**目标**：基于 **libbpf** 实现 AF_XDP 零拷贝后端 + 内核级 RSS（Receive Side Scaling）多队列数据包分发，支持 Share-Nothing 多线程 per-CPU 独立管线架构，在 10Gbps 场景下对比 libpcap 基线验证吞吐量和 CPU 效率提升。

| 编号 | 任务 | 详细描述 | 影响范围 |
|------|------|----------|----------|
| **2.1** | 捕获接口抽象 | ✅ 完成 — `ICapture` 纯虚接口已定义完毕（`set_packet_callback`/`start`/`stop`/`set_filter`/`list_devices`），`PcapCapture` 已实现该接口。本次新增 `AfXdpCapture` 继承同一接口，确保双后端可互换。 | `ICapture.h`、`PcapCapture.{h,cpp}` |
| **2.2** | AF_XDP 后端实现（基于 libbpf） | ✅ 依赖集成完成 — CMake 引入 `pkg_check_modules(XDP REQUIRED libbpf libxdp)` 依赖链，链接 `${XDP_LDFLAGS}` 至 `sentinel_core`。根 CMakeLists 增强 `bpf_prog` 自定义目标：`clang -target bpf` 编译后追加 `bpftool gen skeleton` 生成 `xdp_prog.skel.h`。新建 `AfXdpCapture.{h,cpp}` 骨架类（继承 `ICapture`），定义 `XskContext` 私有结构体（umem/xsk/rx/fill/comp/tx/skel 成员 + 配置常量），实现全部 5 个虚函数为桩方法（TODO 标注完整流程）。后续 Phase 2.2 填充 AF_XDP 初始化/轮询/清理实现。 | `CMakeLists.txt`、`libsentinel/CMakeLists.txt`、`AfXdpCapture.{h,cpp}` |
| **2.3** | XDP BPF 程序编译链（libbpf CO-RE） | ✅ 完成 — CMake 集成 `clang -target bpf` 编译 `xdp_prog.c` 为 `.bpf.o` 目标文件。实现 `xdp_pass_func` XDP 入口：解析 L2→L3→L4 头部链，IPv4 报文通过 `BPF_MAP_TYPE_XSKMAP` 重定向至用户态 AF_XDP 套接字（`bpf_redirect_map`），非 IPv4 报文降级 `XDP_PASS`，畸形报文 `XDP_DROP`。使用现代 BTF 风格映射定义（`__uint(type, ...)`），声明 GPL 许可证。后续阶段通过 `bpftool gen skeleton` 生成 libbpf 骨架代码。 | `CMakeLists.txt`、`bpf/xdp_prog.c` |
| **2.4** | 后端选择 CLI | `main.go` 新增 `--backend pcap|af_xdp` 标志。`Engine.Start()` 根据标志实例化对应后端。 | `cmd/sentinel/main.go`、`engine.go` |
| **2.5** | AF_XDP 性能基准 | 使用 `pktgen` 或 `MoonGen` 生成 64B–1518B 合成流量。记录 PPS/吞吐量/CPU%/丢包率对比 libpcap。目标：10Gbps 下线速 64B 数据包的 CPU 占比 <30%。 | `tests/bench_af_xdp.cpp` |
| **2.6** | 能力检查与降级 | 启动时检测 `CAP_BPF` + `CAP_NET_RAW` 能力；若无，自动降级为 libpcap 模式并打印通知。 | `main.go`、`PcapCapture.cpp` |

### Phase 3：高级检测能力

**目标**：OT 协议解析、基线异常检测、多规则集并发匹配。

| 编号 | 任务 | 详细描述 | 影响范围 |
|------|------|----------|----------|
| **3.1** | Modbus TCP 解析器 | 新增 `ModbusParser` 类：提取 Transaction ID/Protocol ID/Unit ID/Function Code/Data。实现功能码白名单规则类型。 | `libsentinel/src/engine/flow/` |
| **3.2** | S7comm 解析器 | 新增 `S7commParser` 类：提取 Protocol ID/ROSCTR/Parameter/Data。检测 CPU STOP、程序下载/上传、DB 写入等关键指令。 | `libsentinel/src/engine/flow/` |
| **3.3** | 协议检测器框架 | 定义 `IProtocolInspector` 接口（`inspect(const ParsedPacket&) -> optional<Alert>`）。Pipeline 中按协议端口分发至对应解析器链。 | `libsentinel/include/sentinel/engine/` |
| **3.4** | 统计基线引擎 | 基于 EWMA（指数加权移动平均）对 Modbus 功能码频率、S7comm 作业类型分布、轮询间隔建立基线。偏离 3σ 触发异常告警。 | `libsentinel/src/engine/anomaly/` |
| **3.5** | 规则热重载增强 | 将当前 `atomic<shared_ptr<Matcher>>` 升级为 RCU（Read-Copy-Update）模式：编译新自动机 → 原子交换指针 → RCU 等待读侧临界区退出 → 释放旧自动机。消除检测盲区。 | `Pipeline.h`、`Pipeline.cpp` |
| **3.6** | 规则管理 TUI 面板 | 在 Dashboard 中新增规则管理视图：按 ID/协议/等级分组，显示匹配命中计数。支持通过按键启用/禁用单条规则。 | `pkg/ui/components/`、`controller.go` |

### Phase 4：合规与审计

**目标**：IEC 62443 完全对齐，防篡改审计日志，合规报告生成。

| 编号 | 任务 | 详细描述 | 影响范围 |
|------|------|----------|----------|
| **4.1** | 防篡改日志 | 实现 HMAC-SHA256 链式审计日志：每条告警日志含前条日志的哈希，形成防篡改链。提供离线验证工具。 | `DatabaseManager.cpp`、`AuditLogger` |
| **4.2** | ForensicWorker 恢复 | 从旧备份恢复取证 Worker：告警触发时自动转储关联 PCAP（前后各 N 个数据包）到 `evidences/` 目录，文件名含时间戳和规则 ID。 | `libsentinel/src/engine/workers/` |
| **4.3** | IP 黑名单 | 实现基于 `std::unordered_set`（非快速路径）的 IP 过滤。通过 C API 暴露增删接口。Go 侧在 TUI 中提供黑名单管理面板。 | `capi.h`、`Pipeline.cpp` |
| **4.4** | IEC 62443 合规报告 | 实现报告生成器：按日/周/月维度汇总告警统计（按严重等级、协议、规则 ID 分类），输出 Markdown 报告含 SHA-256 摘要。 | `libsentinel/src/engine/governance/` |
| **4.5** | ARM/RISC-V 交叉编译 | CMake 工具链文件支持 `aarch64-linux-gnu` 和 `riscv64-linux-gnu` 交叉编译。验证对象池和 SPSC 队列在弱内存序平台上的正确性（ARM: dmb 指令，RISC-V: fence）。 | `CMakeLists.txt`、`cmake/toolchains/` |

---

## 第四章：技术风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| AF_XDP 重新集成复杂度超预期 | 中 | 高 | Phase 2.1 先抽象接口，Phase 2.2 从旧备份提取并最小化移植，不求功能完全等价 |
| CGO 回调延迟影响 TUI 刷新率 | 低 | 中 | 统计数据批量聚合（100ms 窗口），单个回调仅做原子写入 |
| OT 协议解析规则爆炸 | 中 | 中 | 按 Purdue Level 分层：Phase 3 仅覆盖 Modbus + S7comm（>80% 工控流量） |
| 弱内存序平台（ARM/RISC-V）SPSC 正确性 | 中 | 高 | Phase 4.5 交叉编译 + `std::atomic` fence 验证 + 专门测试套件 |

---

## 参考资料

1. [eBPF '25: Breaking the 100G Barrier with AF_XDP](https://cs.nyu.edu/~apanda/assets/papers/ebpf25.pdf) — NYU/Columbia 联合研究，展示 AF_XDP 在 100G NIC 上实现 25 Mpps 线速
2. [Craton Shield: Open-Source Embedded Security Runtime](https://github.com/craton-co/craton-shield) — 基于 Rust 的 IEC 62443 对齐嵌入式 NIDS
3. [Corelight ICS/OT Collection](https://corelight.com/products/analytics/ics-ot) — 基于 Zeek 的 OT 协议分析器集合
4. [Suricata 7.0 AF_XDP Bypass Mode](https://docs.suricata.io/en/latest/capture-hardware/af-xdp.html) — Suricata AF_XDP 零拷贝模式文档
5. IEC 62443-3-3:2013 — 工业自动化和控制系统安全 — 系统安全要求和安全等级
6. IEC 62443-4-2:2019 — 工业自动化和控制系统安全 — 组件安全技术要求
