# 运维指南

## 1. Linux 权限提升

### 方案 A：`setcap`（推荐）

```bash
sudo setcap cap_net_raw,cap_net_admin=eip ./sentinel-cli
getcap ./sentinel-cli
```

| 能力 | 作用 |
|------|------|
| `cap_net_raw` | 原始套接字 (`AF_PACKET`、`AF_XDP`) |
| `cap_net_admin` | 混杂模式、BPF 过滤器 |
| `=eip` | effective + inheritable + permitted |

### 方案 B：`sudo`

```bash
sudo ./sentinel-cli -i eth0 -r ./configs/rules.yaml
```

### 降级：离线模式

无权限时自动回退：

```bash
./sentinel-cli --offline /path/to/capture.pcap
```

### 故障排查

```bash
# 权限不足 → 输出 EPERM 警告
# 确认 setcap 生效
getcap ./sentinel-cli

# 文件系统不支持 → 移至 ext4/xfs
# 重新编译后失效 → 重新 setcap
# SELinux 拦截 → sudo setenforce 0（临时）
```

---

## 2. 性能调优

### 2.1 瓶颈识别

| 症状 | 原因 | 检查命令 |
|------|------|----------|
| 网卡丢包 `ps_drop` 增加 | 内核缓冲区不足 | `ethtool -S eth0 \| grep drop` |
| `SPSCQueue::size()` > 5000 | 解析线程不足 | `-w` 参数增大 |
| 单核 CPU 100% | 线程未绑核 | 检查 `setCoreId()` |
| 告警队列积压 > 20000 | 磁盘 I/O 瓶颈 | `iostat -x 1` |

### 2.2 系统级参数

```bash
# 增大网卡环形缓冲区
ethtool -g eth0                          # 查看当前值
ethtool -G eth0 rx 4096 tx 4096         # 设为最大值

# 内核网络参数
sysctl -w net.core.rmem_max=26214400
sysctl -w net.core.rmem_default=26214400

# RPS 分散软中断负载
echo 7 > /sys/class/net/eth0/queues/rx-0/rps_cpus

# 中断绑定（将网卡中断绑定到非 Pipeline 使用的核心）
# 查看 /proc/interrupts 找到 eth0 的 IRQ 号
echo <cpu_mask> > /proc/irq/<irq_num>/smp_affinity
```

### 2.3 应用层参数

| 参数 | 默认值 | 位置 | 调优建议 |
|------|--------|------|---------|
| `ring_buffer_size` | 65536 | `binding.go:118` | 高吞吐场景增至 131072 |
| SPSCQueue 容量 | 65536 | `SPSCQueue.h:13` 模板参数 | 容量 = 2^n |
| `BATCH_RESERVE_SIZE` | — | 当前未生效（无 batch 机制） | N/A |
| `UI_REFRESH_INTERVAL_MS` | 150 | `PacketPipeline.cpp:10` | 降低刷新频率减少回调开销 |
| `SUPPRESSION_WINDOW_MS` | 2000 | `SecurityEngine.cpp:86` | 增大 = 更少重复告警，但可能漏报 |
| `NUM_FRAMES` | 4096 | `EBPFCapture.cpp:19` | AF_XDP UMEM 帧数，增大减少填充操作 |
| ObjectPool 预分配 | 20000 | `NetworkTypes.h:24` | 按并发连接数调整 |

### 2.4 SQLite 参数

```sql
PRAGMA cache_size = 10000;       -- 增大页面缓存 (默认 2000 页)
PRAGMA synchronous = NORMAL;     -- 降低同步级别 (已默认)
PRAGMA journal_mode = WAL;       -- WAL 模式 (已默认)
VACUUM;                          -- 定期回收空间
```

### 2.5 CPU 亲和性配置

核心分配策略（`capi_impl.cpp:128-129`）：核心 0 留给捕获线程和操作系统中断处理，工作线程从核心 1 开始逐个绑定。

```cpp
pipeline->setCoreId(static_cast<int>(i + 1));
```

### 2.6 内存池调整

```cpp
// ObjectPool 预分配数量，在 NetworkTypes.h 中修改：
PacketPool::instance().reserve(50000);  // 从默认 20000 增至 50000
```

### 2.7 数据库存储路径

默认路径：`/tmp/sentinel-flow/sentinel_data.db`。通过 `DatabaseManager::init(path)` 自定义。

---

## 3. 典型部署场景

### 场景 W：万兆核心网 IDS

```
硬件: 16 核, 32GB, SSD RAID
预期: 8 Gbps, 100k pps
配置:
  -w 8 --ebpf                        # 8 个 Pipeline 线程 + AF_XDP
  ethtool -G eth0 rx 4096            # 网卡缓冲最大
  SPSCQueue 容量: 131072             # 编译期修改
  ObjectPool 预分配: 100000          # 高并发连接
  关闭 HTTP/TLS 解析                 # 编译期宏控制
  SQLite: synchronous=NORMAL         # 已默认
```

### 场景 R：树莓派 4B 边缘节点

```
硬件: 4 核 ARM, 2GB, MicroSD
预期: 100 Mbps, 10k pps
配置:
  -w 2 --offline /path/to/pcap       # 离线分析模式
  仅启用 TCP/UDP 解析                 # 根据需求
  ObjectPool 预分配: 5000
  SQLite: cache_size=2000
  不使用 eBPF (libpcap 模式)
```

---

## 4. 监控诊断

### 内部指标

| 指标 | 获取方式 |
|------|---------|
| 队列长度 | `SPSCQueue::size()` |
| 告警队列积压 | `DatabaseManager::alertQueue.size()` |
| 内核丢包计数 | `pcap_stats()` |
| 吞吐量/丢包 | Go Dashboard 终端实时输出 |
| 全局统计 | `GlobalStats::snapshot()` → `EngineStats` 回调 |

### 外部工具

```bash
perf top -p $(pgrep sentinel-cli)    # 热点函数
iostat -x 1                           # 磁盘 I/O
mpstat -P ALL 1                       # 各核心负载
```

---

## 5. 取证文件管理

输出目录：`evidences/`（项目根目录）。

文件命名：`batch_YYYYMMDD_HHMMSS_count_N.pcap`

长期运行后需手动清理：

```bash
# 保留最近 100 个文件
ls -t evidences/batch_*.pcap | tail -n +101 | xargs rm -f

# 或按磁盘空间阈值通过 cron job 管理
```
