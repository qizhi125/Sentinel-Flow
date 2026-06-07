# 运维指南

## 1. Linux 权限提升机制

### 概述

Sentinel-Flow CLI 工具 (`sentinel-cli`) 在 Linux 平台上需要 `CAP_NET_RAW` 和 `CAP_NET_ADMIN` 权限才能通过原始套接字 (`AF_PACKET`) 或 AF_XDP 捕获网络流量。为了避免以 root 身份运行整个进程（可能引发安全风险或 Wayland/X11 显示问题），推荐使用 `setcap` 为二进制文件授予所需能力，使其以普通用户身份运行但具备网络捕获权限。

### 权限探测

工具启动时会尝试创建 `AF_PACKET` 原始套接字来检测是否已具备所需权限。若创建失败（通常返回 `EPERM`），将提示用户权限不足并给出提权指引。

### 推荐方案：使用 setcap 授予能力

为编译生成的二进制文件一次性赋予网络能力，后续无需 `sudo` 即可运行：

```bash
# 赋予捕获原始网络包和管理网络设备的能力
sudo setcap cap_net_raw,cap_net_admin=eip ./bin/sentinel-cli

# 验证能力已附加
getcap ./bin/sentinel-cli
```

之后直接运行即可：

```bash
./bin/sentinel-cli -i eth0 -r ./configs/rules.yaml
```

### 能力说明

| 能力 | 作用 |
|------|------|
| `cap_net_raw` | 允许使用原始套接字（`AF_PACKET`、`AF_XDP`） |
| `cap_net_admin` | 允许执行网络管理操作（设置混杂模式、BPF 过滤器等） |
| `=eip` | 设置有效（effective）、继承（inheritable）和允许（permitted）位 |

### 备选方案：使用 sudo 运行

若不方便使用 `setcap`（例如二进制文件所在文件系统不支持扩展属性），也可通过 `sudo` 直接运行：

```bash
sudo ./bin/sentinel-cli -i eth0 -r ./configs/rules.yaml
```

**注意**：在 Wayland 环境下，`sudo` 可能导致终端或图形会话异常（如无法访问显示服务），但对于纯 CLI 工具通常无影响。

### 提权失败或降级处理

若未授予能力且未使用 `sudo`，引擎将无法打开实时网卡。此时系统会输出警告，并自动进入**离线模式**，仅支持导入 PCAP 文件进行分析：

```bash
./bin/sentinel-cli --offline /path/to/capture.pcap
```

### 非 Linux 平台

在 macOS 或 Windows 上，由于不支持 `AF_PACKET`，实时捕获功能不可用。工具将强制以离线模式运行，仅能处理 PCAP 文件。

### 安全考量

- **最小权限原则**：仅授予必要的网络能力，避免完全 root 权限。
- **文件完整性**：`setcap` 将能力写入文件扩展属性，请确保可执行文件不被恶意篡改。
- **能力继承**：子进程不会自动继承父进程的能力，避免权限泄漏。

### 故障排查

#### setcap 命令不存在

```bash
# Debian/Ubuntu
sudo apt install libcap2-bin

# Fedora
sudo dnf install libcap
```

#### 文件系统不支持扩展属性

某些文件系统（如 tmpfs、NFS）可能不支持 `setcap`。请将二进制文件移动至支持的文件系统（如 ext4、xfs）再执行命令。

#### 运行后仍提示权限不足

检查能力是否被正确附加：

```bash
getcap ./bin/sentinel-cli
# 应输出：./bin/sentinel-cli cap_net_raw,cap_net_admin=eip
```

若二进制文件被更新（重新编译），需重新执行 `setcap`。

#### SELinux/AppArmor 拦截

强制访问控制系统可能阻止能力生效。可临时关闭 SELinux（`setenforce 0`）测试，或配置相应策略允许 `cap_net_raw`。

---

## 2. 性能调优指南

### 概述

Sentinel-Flow 在设计之初就将高性能作为核心目标，通过无锁队列、零拷贝内存池、CPU 亲和性绑定等技术实现了高吞吐量下的稳定处理。本文档针对不同部署场景，提供系统级和应用级的性能调优建议。

### 瓶颈分析

| 瓶颈类型 | 表现 | 可能原因 |
|----------|------|----------|
| 网卡丢包 | `pcap_stats` 显示 `ps_drop` 增加 | 内核缓冲区不足、中断处理过载 |
| 队列积压 | `SPSCQueue::size()` 持续 > 5000 | 解析线程处理能力不足 |
| CPU 满载 | 单核使用率 100% | 解析线程未绑定核心、协议解析过重 |
| 数据库写入延迟 | 告警队列积压 > 20000 | 磁盘 I/O 瓶颈、事务批处理未生效 |

### 系统级调优

#### 网卡与内核参数

**增大环形缓冲区**：

```bash
# 查看当前值
ethtool -g eth0
# 设置为最大（例如 4096）
ethtool -G eth0 rx 4096 tx 4096
```

**调整内核网络参数**：

```bash
# 增加 netlink 缓冲区大小
sysctl -w net.core.rmem_max=26214400
sysctl -w net.core.rmem_default=26214400

# 开启 RPS (Receive Packet Steering) 分散软中断负载
echo 7 > /sys/class/net/eth0/queues/rx-0/rps_cpus
```

#### CPU 亲和性与线程绑定

- **解析线程绑定**：通过 `PacketPipeline::setCoreId()` 将每个管线绑定到独立物理核心。
- **中断绑定**：将网卡中断绑定到非解析线程使用的核心，避免干扰。

示例（在 `capi_impl.cpp` 中配置）：

```cpp
// 为 4 个管线分配核心 2,3,4,5
for (int i = 0; i < 4; ++i) {
    pipeline->setCoreId(2 + i);
}
```

#### 内存与文件系统

- **数据库存储**：将 SQLite 文件放在高速 SSD 上，避免使用机械硬盘。
- **取证文件目录**：建议独立挂载点，设置 `noatime` 挂载选项减少写操作。
- **预分配内存池**：根据预期并发连接数调整 `PacketPool` 预分配数量（默认 20000）。

### 应用层调优

#### 协议解析控制

通过配置或编译选项关闭非必要的深度解析，减少 CPU 开销：

| 场景 | 推荐配置 |
|------|----------|
| 万兆核心网（仅需 L3/L4 统计） | 关闭 HTTP、TLS、ICMP 解析 |
| 安全审计（需深度检测） | 全部开启 |
| 低配置硬件（树莓派等） | 仅开启 TCP/UDP，关闭 L7 解析 |

#### 队列与背压参数

- **队列容量**：`SPSCQueue` 默认容量 65536，可通过模板参数调整。
- **背压阈值**：`PcapCapture` 中 `isCongested` 判断基于队列长度 > 5000。

#### 告警抑制窗口

`SecurityEngine` 的 `SUPPRESSION_WINDOW_MS` 默认 2000ms，可调整以减少重复告警，但也可能漏掉短时间内再次触发的威胁。

#### 批处理大小

- **解析批处理**：`PacketPipeline` 中 `BATCH_RESERVE_SIZE` 默认 5000。
- **数据库批处理**：一次事务最多 1000 条告警。

#### 数据库优化

```sql
-- 增加缓存大小（默认 2000 页，约 2MB）
PRAGMA cache_size = 10000;

-- 调整同步级别（慎用，可能降低安全性）
PRAGMA synchronous = NORMAL;

-- 定期执行 VACUUM 回收空间
VACUUM;
```

### 监控与诊断

#### 内置指标

- **队列长度**：通过 `queue->size()` 可观察解析压力。
- **告警队列积压**：`DatabaseManager` 的 `alertQueue.size()` 反映 I/O 压力。
- **丢包统计**：`pcap_stats` 获取内核丢弃计数。
- **终端输出**：Go 侧实时打印吞吐量和丢包数。

#### 外部工具

- **`perf`**：分析热点函数。
- **`iostat`**：监控磁盘 I/O。
- **`mpstat`**：观察各核心负载分布。

### 典型场景调优案例

#### 场景一：万兆 IDS 中心

- 硬件：16 核 CPU，32GB 内存，SSD RAID
- 预期流量：8 Gbps，100k pps
- 调优措施：
  - 网卡队列 4096
  - 绑定 8 个解析线程到独立核心（core 2-9）
  - 关闭 HTTP/TLS 解析（或使用 BPF 过滤无关流量）
  - 数据库使用 `synchronous=NORMAL`，WAL 模式
  - 预分配内存池 100000

#### 场景二：树莓派 4B 边缘检测

- 硬件：4 核 ARM，2GB 内存，MicroSD
- 预期流量：100 Mbps，10k pps
- 调优措施：
  - 仅启用 TCP/UDP 解析
  - 关闭数据库告警落盘（仅输出到终端或日志）
  - 批处理大小降至 1000
  - 使用 libpcap 模式，避免 eBPF 内存开销

### 性能基线参考

| 指标 | 参考值 |
|------|--------|
| 单核解析吞吐 | 约 200k pps（TCP 无载荷） |
| 数据库写入速度 | 约 5000 条/秒（SSD） |
| 内存占用（20000 规则） | 约 500 MB |
| 端到端延迟 | < 1ms（解析 + 检测） |

### 常见问题

#### 问题：捕获线程显示 `pcap_next_ex` 返回 0 但无数据

- 可能是网卡未处于混杂模式或 BPF 过滤过于严格，检查 `setFilter` 设置。

#### 问题：告警队列持续积压，内存增长

- 检查数据库写入线程是否正常工作（`workerLoop` 运行中）。
- 确认 `synchronous` 设置是否过高导致 I/O 阻塞。
- 考虑减小告警等级阈值，减少写入量。

#### 问题：终端输出刷新过快导致可读性差

- 调整 Go 侧统计回调的打印间隔，或使用 `\r` 单行刷新（当前已实现）。
