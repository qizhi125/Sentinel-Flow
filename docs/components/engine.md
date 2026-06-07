# 引擎子系统

## 1. AC 自动机多模式匹配引擎

### 概述

`AhoCorasick` 类实现了多模式匹配算法，用于在数据包载荷中快速检测多个威胁特征。与传统的正则表达式逐条匹配相比，AC 自动机的匹配时间复杂度与文本长度成线性关系，与规则数量无关，非常适合高吞吐量环境。

### 数据结构

#### 节点定义

```cpp
struct Node {
    Node* next[256];          // 状态转移表 (每个字节一个槽位)
    Node* fail = nullptr;     // 失败指针
    std::vector<int> ruleIds; // 当前节点匹配的规则 ID 列表
    Node() { std::memset(next, 0, sizeof(next)); }
};
```

- `next[256]`：支持 0-255 所有字节值的快速转移，无需哈希。
- `fail`：构建时填充，用于失配时的状态回退。
- `ruleIds`：存储以当前节点结尾的规则 ID（支持多规则共享相同后缀）。

### 算法实现

#### 插入规则

```cpp
void insert(const std::string& pattern, int ruleId) {
    Node* curr = root.get();
    for (uint8_t c : pattern) {
        uint8_t idx = static_cast<uint8_t>(std::toupper(c)); // 大小写不敏感
        if (!curr->next[idx]) {
            auto newNode = std::make_unique<Node>();
            curr->next[idx] = newNode.get();
            allNodes.push_back(std::move(newNode));
        }
        curr = curr->next[idx];
    }
    curr->ruleIds.push_back(ruleId);
}
```

#### 构建失败指针

使用 BFS 遍历：对于每个节点，其子节点的 `fail` 指针设置为 `u->fail->next[i]`。同时将 `fail` 节点的规则 ID 合并到当前节点，实现后缀匹配。对于缺失的转移，预先填充为 `fail` 节点的对应转移，加速匹配。

#### 匹配过程

```cpp
const std::vector<int>* match(const std::vector<uint8_t>& data) const {
    Node* curr = root.get();
    for (uint8_t c : data) {
        uint8_t idx = static_cast<uint8_t>(std::toupper(c));
        curr = curr->next[idx];
        if (!curr->ruleIds.empty())
            return &curr->ruleIds;  // 返回命中的所有规则
    }
    return nullptr;
}
```

- 遍历载荷每个字节，根据当前状态转移。
- 若当前节点有规则 ID，立即返回所有匹配规则。
- 时间复杂度 $O(N)$，N 为载荷长度。

### 热重载机制

在 `SecurityEngine` 中，规则修改后调用 `compileRules()` 重建自动机：

1. 在锁外构建新自动机（避免阻塞数据面）
2. 通过 `std::swap` 原子切换新旧指针
3. 旧自动机在锁外析构，消除销毁开销对数据面的影响

### 内存管理

- 所有节点使用 `std::unique_ptr` 管理，自动释放。
- `allNodes` 向量保存所有节点指针，保证析构时能正确释放所有内存。
- 节点数量与规则总长度成正比，每个规则字符对应一个节点（共享前缀的规则共用节点）。

### 性能特性

- **匹配速度**：$O(N)$，独立于规则数量，实测每秒可处理数万条规则。
- **内存占用**：每个节点 256 个指针（约 2KB），规则数量较多时需注意内存，例如 10 万字符规则树约占用 200MB。
- **大小写不敏感**：通过字符转大写实现，性能影响极小。
- **规则更新**：热重载时新旧引擎切换不影响正在进行的匹配，旧引擎在操作结束后延迟释放。

### 使用限制

- 规则模式长度不宜过短（建议 ≥ 3 字节），避免产生大量误报。
- 目前不支持通配符、范围匹配等高级语法，需扩展为正则引擎。

---

## 2. 解析流水线设计

### 概述

`PacketPipeline` 是 Sentinel-Flow 数据面的核心处理单元，每个实例运行在独立线程中，负责从 SPSC 队列获取原始报文，执行协议解析、安全检测，并将结果通过回调函数批量投递给上层（Go 控制面或内部日志模块）。通过 CPU 亲和性绑定、批处理缓冲和背压感知，实现在高吞吐环境下的稳定处理。

### 类设计

```cpp
namespace sentinel::engine {

class PacketPipeline {
public:
    PacketPipeline();
    ~PacketPipeline();

    void setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue);
    void setInspector(sentinel::engine::IInspector* inspector);
    void setCoreId(int coreId);

    // 设置回调函数
    using BatchCallback = std::function<void(std::shared_ptr<std::vector<ParsedPacket>>)>;
    using ThreatCallback = std::function<void(const Alert&, const ParsedPacket&)>;
    using StatsCallback = std::function<void(uint64_t bytesProcessed)>;
    void setCallBack(BatchCallback batchCb, ThreatCallback threatCb, StatsCallback statsCb);

    void startPipeline();
    void stopPipeline();
    void wait();

private:
    void run();
    void flushBatch(uint64_t& bytesAccumulator);

    sentinel::common::SPSCQueue<RawPacket>* inputQueue = nullptr;
    sentinel::engine::IInspector* m_inspector = nullptr;
    std::atomic<bool> running{false};
    int m_coreId = -1;

    std::shared_ptr<std::vector<ParsedPacket>> currentBatch;
    std::thread workerThread;

    BatchCallback m_batchCb;
    ThreatCallback m_threatCb;
    StatsCallback m_statsCb;
};

} // namespace sentinel::engine
```

### CPU 亲和性绑定

```cpp
if (m_coreId >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(m_coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}
```

- 通过 `setCoreId()` 指定目标 CPU 核心。
- 在 `run()` 开始时调用 `pthread_setaffinity_np` 绑定线程到指定核心，减少上下文切换，提高缓存局部性。

### 核心处理循环

```cpp
while (running.load(std::memory_order_acquire)) {
    try {
        // 1. 从队列获取报文（超时 100ms）
        auto rawOpt = inputQueue->popWait(std::chrono::milliseconds(100));

        // 2. 定期批量触发回调（150ms 或 5000 条报文）
        if (elapsedMs > UI_REFRESH_INTERVAL_MS || currentBatch->size() >= BATCH_RESERVE_SIZE) {
            flushBatch(bytesAccumulator);
        }

        if (!rawOpt) continue;

        // 3. 解析报文
        auto parsedOpt = PacketParser::parse(raw);
        if (!parsedOpt) continue;

        // 4. 威胁检测
        if (m_inspector) {
            auto alertOpt = m_inspector->inspect(parsed);
            if (alertOpt) {
                DatabaseManager::instance().saveAlert(*alertOpt);
                if (m_threatCb) m_threatCb(*alertOpt, parsed);
            }
        }

        // 5. 释放数据块引用
        parsed.block.reset();
    } catch (const std::exception& e) {
        // 防止单个报文异常导致线程崩溃
    }
}
flushBatch(bytesAccumulator);
```

### 批处理机制

- **双条件触发**：数量达到 5000 条，或时间超过 150ms。
- **批量投递优势**：减少回调调用频率，使用 `std::shared_ptr` 传递批量数据，零拷贝移交。

### 组件交互

| 接口 | 说明 |
|------|------|
| 输入源 | `setInputQueue()` 绑定 SPSC 队列 |
| 检测引擎 | `setInspector()` 注入 `SecurityEngine` |
| ThreatCallback | 威胁告警 → C API → Go 回调 |
| StatsCallback | 定期统计上报 → Go 仪表盘 |

### 性能调优参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `BATCH_RESERVE_SIZE` | 5000 | 批处理缓冲区预分配容量 |
| `UI_REFRESH_INTERVAL_MS` | 150 | 强制刷新时间间隔（毫秒） |
| 队列超时 | 100 ms | `popWait` 等待时间 |

---

## 3. 规则解析与策略引擎

### 概述

Sentinel-Flow 的规则引擎基于 Aho-Corasick 自动机实现多模式匹配，支持动态加载与热重载。Go 控制面使用 YAML 配置文件进行规则下发，通过 C API 调用 `sentinel_engine_add_rule` 逐条下发规则，`compileRules()` 触发 AC 自动机重建。

### 规则数据结构

```cpp
struct IdsRule {
    int id;                 // 唯一标识
    bool enabled;           // 是否启用
    std::string protocol;   // 协议类型 (TCP/UDP/ICMP/ANY)
    std::string pattern;    // 匹配模式 (字节序列)
    Alert::Level level;     // 告警等级
    std::string description;// 规则描述
};
```

### YAML 规则配置（当前主要方式）

```yaml
rules:
  - id: 1001
    enabled: true
    protocol: "ANY"
    pattern: "sentinel_test_attack"
    level: 4
    description: "Simulated Attack: sentinel_test_attack payload detected"
  - id: 1002
    enabled: true
    protocol: "HTTP"
    pattern: "admin' OR 1=1"
    level: 4
    description: "SQL Injection Payload Detected in HTTP"
```

添加规则后调用 `sentinel_engine_reload_rules` 触发 AC 自动机重建。

### Snort 规则导入（历史支持）

历史 Qt GUI 组件实现了 Snort 规则文件的智能解析，支持以下字段：

- `msg:"..."` → 规则描述
- `content:"..."` → 匹配模式（支持 `|` 包裹的十六进制）
- `classtype:...` → 用于等级推导
- `priority:...` → 辅助等级判断
- `sid:...` → 作为规则 ID 的一部分

**规则定级策略**（基于历史实现）：

- 包含 `ransomware`、`cobalt strike`、`reverse shell`、`apt`、`meterpreter` → `Critical`
- 包含 `trojan`、`malware-cnc`、`backdoor`、`c2`、`botnet` → `High`
- 包含 `attempted`、`exploit`、`dos`、`suspicious` → `Medium`
- 包含 `policy`、`recon`、`scan`、`leak` → `Low`

### 热重载特性

- **无中断**：AC 自动机指针切换是原子操作，数据面线程在匹配时始终读取有效指针。
- **旧引擎延迟销毁**：旧自动机在 `compileRules` 函数返回后随局部变量析构，避免在锁内执行耗时的节点释放。
- **Go CLI 文件监听**：使用 `fsnotify` 监听 YAML 规则文件变化，500ms 防抖，自动触发 `ClearRules()` → `LoadRules()` → 编译。

### 告警抑制机制

为避免同一 IP 同一规则短时间内重复告警，`SecurityEngine` 实现了基于时间窗口的去重：

- 抑制窗口：`SUPPRESSION_WINDOW_MS = 2000` 毫秒
- 缓存键：`srcIp + "_" + ruleId`
- 命中规则后，记录当前时间戳；窗口期内再次命中同一规则则丢弃告警。

### 规则持久化（可选）

`DatabaseManager` 提供以下 API，可用于规则的数据库存储（当前 CLI 模式下未强制使用）：

```sql
CREATE TABLE rules (
    id INTEGER PRIMARY KEY,
    enabled INTEGER,
    protocol TEXT,
    pattern TEXT,
    level INTEGER,
    description TEXT
);
```

---

## 4. 终端仪表盘 (Dashboard)

### 概述

`Dashboard` 是 Sentinel-Flow Go CLI 的实时终端可视化组件，将 C++ 数据面的运行状态、告警事件以 ANSI 彩色界面形式呈现。运行在独立的 goroutine 中，每秒刷新一次，通过原子操作从 C++ 回调异步接收数据。

### 核心数据结构

```go
type Dashboard struct {
    mu sync.Mutex
    startTime time.Time
    iface, mode, backend string
    workers, rules int
    verbose bool

    // 累积计数器（来自 C++ 的快照）
    packetsRecv, packetsDrop, bytesRecv uint64

    // 增量计算
    lastBytesRecv uint64
    lastBytesTime time.Time
    throughputBps float64

    // 告警循环缓冲区（最多 5 条）
    alerts   []Report
    alertMax int

    // 渲染状态
    frameCount int
    liveLines  int
}
```

### 全局限原子变量

为避免 C++ 回调在持有互斥锁时长时间阻塞，使用全局原子变量作为 C++ 回调与 Go 渲染循环之间的无锁桥梁：

```go
var (
    globalStatsRecv  atomic.Uint64
    globalStatsDrop  atomic.Uint64
    globalStatsBytes atomic.Uint64
    globalStatsDirty atomic.Bool
)
```

### 终端界面布局

```
╔═════════════════════════════════════════════════════╗
║  Sentinel-Flow  v2.0.0                              ║
║  Network Intrusion Detection System                 ║
╠═════════════════════════════════════════════════════╣
║  Interface: eth0          Mode:  Live (libpcap)     ║
║  Workers:   4             Rules: 12 loaded          ║
║  Backend:   libpcap                                ║
╚═════════════════════════════════════════════════════╝

┌── Live Statistics ──────────────────────────────────┐
│  Packets    12.5K recv  │  Dropped       123 (0.98%) │
│  Bytes      3.2M        │  Throughput  1.2 MB/s     │
│  Uptime          1h23m  │  Alerts            5      │
├── Recent Alerts ────────────────────────────────────┤
│  [HIGH] 15:04:05  Rule 1001  SQL Injection Detected │
│  [MED]  15:03:52  Rule 1002  XSS Attempt            │
└──────────────────────────────────────────────────────┘
  Press Ctrl+C to stop
```

### 颜色编码

| 等级 | ANSI 序列 | 效果 |
|------|-----------|------|
| CRIT | `\033[1;35m` | 粗体品红 |
| HIGH | `\033[1;31m` | 粗体红色 |
| MED | `\033[33m` | 黄色 |
| LOW | `\033[32m` | 绿色 |
| INFO | `\033[37m` | 白色 |

### 刷新机制

- **渲染循环**：每秒 1 次，使用 `time.Ticker`。
- **光标控制**：渲染时隐藏光标（`\033[?25l`），退出时恢复。
- **ANSI 转义**：使用 `\033[K`（清除行）和 `\033[%dA`（向上移动）实现原地重绘。
- **吞吐量计算**：最小更新间隔 0.5 秒，防止抖动。

### C++ 回调集成

Dashboard 通过 `binding.go` 中的 `SetDashboard()` 注入为全局单例。回调函数仅执行原子 `Store` 操作，不持有 Dashboard 的内部互斥锁，避免阻塞 C++ 检测线程。

### 关机摘要

引擎收到 SIGINT/SIGTERM 时打印最终统计汇总：

```
── Shutdown Summary ───────────────────────────────
  Uptime:      1h23m45s
  Received:    125000 packets
  Dropped:     123 packets (0.10%)
  Alerts:      5 total
  Exit code:   0 (clean)
──────────────────────────────────────────────────
```
