
# 规则解析与策略引擎

## 概述

Sentinel-Flow 的规则引擎基于 Aho-Corasick 自动机实现多模式匹配，支持动态加载、热重载及 Snort 规则导入。规则被存储于 SQLite 数据库，系统启动时自动加载并构建 AC 自动机。

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

### 内置规则

系统启动时，若数据库为空，可从默认路径导入预置规则（当前版本未内置预置规则，需通过 UI 导入）。

### 用户自定义规则

通过 GUI 的“规则策略”页面（`IdsRulesTab`）手动添加：
- **规则描述**：用于 UI 显示
- **特征码**：待匹配的字节序列（支持十六进制，例如 `"|0A 0B|GET"`）

添加后自动保存至数据库，并触发引擎重载。

### Snort 规则导入

`IdsRulesTab::onImportRules()` 实现了 Snort 规则文件的智能解析，支持以下字段：
- `msg:"..."` → 规则描述
- `content:"..."` → 匹配模式（支持 `|` 包裹的十六进制）
- `classtype:...` → 用于等级推导
- `priority:...` → 辅助等级判断
- `sid:...` → 作为规则 ID 的一部分

**导入流程**：
1. 用户选择 `.rules` 文件。
2. 逐行读取，忽略注释和空行。
3. 正则提取 `msg`、`content`、`classtype`、`priority`、`sid`。
4. 解析 `content` 中的十六进制转义（`|...|` 内的十六进制字节），转换为原始字节序列。
5. 根据 `classtype` 和关键词（如 `ransomware`、`backdoor`）自动定级。
6. 生成 `IdsRule` 对象，批量写入数据库，并添加到内存引擎。
7. 调用 `SecurityEngine::compileRules()` 热重载 AC 自动机。

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

## 规则持久化

`DatabaseManager` 提供以下 API：
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

## 规则管理界面

`IdsRulesTab` 提供：
- 规则列表展示（ID、等级、特征、描述）
- 添加自定义规则
- 导入 Snort 规则文件
- 删除规则
- 清空所有规则
- 按威胁等级或来源筛选

操作后自动调用 `DatabaseManager` 持久化并触发 `SecurityEngine::compileRules()` 热加载。

## 性能考量

- **AC 自动机内存占用**：每个节点有 256 个 `Node*` 指针（约 2KB/节点）。规则较多时需注意内存。
- **规则数量上限**：实测可支持数万条规则，匹配性能稳定在 $O(N)$。
- **导入优化**：批量导入时使用 `saveRulesTransaction` 减少数据库 I/O。
```

---

此更新详细描述了规则结构、导入解析、AC 自动机构建、热加载、抑制机制和持久化，完全基于源码实现。如有需要进一步调整，请告知。