# 规则解析与策略引擎

## 概述

Sentinel-Flow 的规则引擎基于 Aho-Corasick 自动机实现多模式匹配，支持动态加载与热重载。规则可通过多种方式导入：当前 Go 控制面使用 **YAML 配置文件** 进行规则下发；历史版本曾支持 Snort 规则导入及 Qt GUI 界面管理。本文档描述规则数据结构、解析逻辑及引擎集成方式，其中核心解析算法仍适用于当前架构。

> **当前架构说明**：Go CLI 工具通过 C API 调用 `sentinel_engine_add_rule` 逐条下发规则，规则定义存储于 `configs/rules.yaml` 文件中。`DatabaseManager` 的规则持久化功能在纯 CLI 模式下为可选组件。

## 规则数据结构

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

## 规则来源与导入

### YAML 配置文件（当前主要方式）

Go 控制面启动时解析 YAML 规则文件，通过 CGO 调用 C API 逐条添加规则。示例配置：

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

`IdsRulesTab::onImportRules()`（历史 Qt GUI 组件）实现了 Snort 规则文件的智能解析，支持以下字段：

- `msg:"..."` → 规则描述
- `content:"..."` → 匹配模式（支持 `|` 包裹的十六进制）
- `classtype:...` → 用于等级推导
- `priority:...` → 辅助等级判断
- `sid:...` → 作为规则 ID 的一部分

**导入流程**：

1. 读取 `.rules` 文件，逐行解析。
2. 正则提取 `msg`、`content`、`classtype`、`priority`、`sid`。
3. 解析 `content` 中的十六进制转义（`|...|` 内的十六进制字节），转换为原始字节序列。
4. 根据 `classtype` 和关键词自动定级（见下文）。
5. 生成 `IdsRule` 对象，批量写入数据库，并添加到内存引擎。
6. 调用 `SecurityEngine::compileRules()` 热重载 AC 自动机。

**规则定级策略**（基于源码实现）：

- 包含 `ransomware`、`cobalt strike`、`reverse shell`、`apt`、`meterpreter` → `Critical`
- 包含 `trojan`、`malware-cnc`、`backdoor`、`c2`、`botnet` → `High`
- 包含 `attempted`、`exploit`、`dos`、`suspicious` → `Medium`
- 包含 `policy`、`recon`、`scan`、`leak` → `Low`
- 其他根据 `priority`（1→Medium, 2→Low, else→Info）或 `classtype` 判断。

## AC 自动机构建与热加载

### 构建过程

`SecurityEngine::compileRules()` 执行以下步骤：

1. 获取当前规则快照（使用读锁）。
2. 创建新的 `AhoCorasick` 实例。
3. 遍历所有启用且非空模式的规则，调用 `insert(pattern, ruleId)` 插入。
4. 调用 `build()` 构建失败指针并合并后缀规则。
5. 使用写锁交换新旧自动机指针（`std::unique_ptr` 原子交换）。
6. 旧自动机在锁外析构，避免阻塞数据面。

### 热加载特性

- **无中断**：AC 自动机指针切换是原子操作，数据面线程在匹配时始终读取有效指针。
- **旧引擎延迟销毁**：旧自动机在 `compileRules` 函数返回后随局部变量析构，避免在锁内执行耗时的节点释放。

### 匹配流程

`SecurityEngine::inspect()` 调用 `acDetector->match(payloadData)` 返回命中的规则 ID 列表（当前仅取第一个）。若规则协议匹配且不在告警抑制窗口内，则生成告警并记录。

## 告警抑制机制

为避免同一 IP 同一规则短时间内重复告警，`SecurityEngine` 实现了基于时间窗口的去重：

- 抑制窗口：`SUPPRESSION_WINDOW_MS = 2000` 毫秒
- 缓存键：`srcIp + "_" + ruleId`
- 命中规则后，记录当前时间戳；窗口期内再次命中同一规则则丢弃告警。

## 规则持久化（可选）

`DatabaseManager` 提供以下 API，可用于规则的数据库存储（当前 CLI 模式下未强制使用）：

- `saveRule()`：单条规则插入或替换
- `saveRulesTransaction()`：批量导入，使用事务提升性能
- `loadRules()`：加载所有规则，返回 `std::vector<IdsRule>`
- `deleteRule()`：删除指定 ID 规则
- `clearRules()`：清空规则表

数据库表 `rules` 结构：

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

## 性能考量

- **AC 自动机内存占用**：每个节点有 256 个 `Node*` 指针（约 2KB/节点）。规则较多时需注意内存。
- **规则数量上限**：实测可支持数万条规则，匹配性能稳定在 $O(N)$。
- **导入优化**：批量导入时使用 `saveRulesTransaction` 减少数据库 I/O。

