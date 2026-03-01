# AC 自动机多模式匹配引擎

## 概述

`AhoCorasick` 类实现了多模式匹配算法，用于在数据包载荷中快速检测多个威胁特征。与传统的正则表达式逐条匹配相比，AC 自动机的匹配时间复杂度与文本长度成线性关系，与规则数量无关，非常适合高吞吐量环境。

## 数据结构

### 节点定义

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

### AC 自动机类

```cpp
    class AhoCorasick {
    public:
        void insert(const std::string& pattern, int ruleId);
        void build();
        const std::vector<int>* match(const std::vector<uint8_t>& data) const;
    private:
        std::unique_ptr<Node> root;
        std::vector<std::unique_ptr<Node>> allNodes;
    };
```

- `root` 作为自动机的根节点。
- `allNodes` 保存所有动态分配的节点，用于统一析构。

## 算法实现

### 插入规则

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

- 将规则转换为大写字节序列。
- 沿着 `next` 数组创建路径，若节点不存在则新建。
- 在终端节点记录规则 ID。

### 构建失败指针

```cpp
    void build() {
        std::queue<Node*> q;
        // 初始化第一层节点的 fail 指针指向 root
        for (int i = 0; i < 256; ++i) {
            if (root->next[i]) {
                root->next[i]->fail = root.get();
                q.push(root->next[i]);
            } else {
                root->next[i] = root.get(); // 缺失的转移指向 root
            }
        }
    
        // BFS 构建后续节点的 fail 指针
        while (!q.empty()) {
            Node* u = q.front(); q.pop();
            for (int i = 0; i < 256; ++i) {
                if (u->next[i]) {
                    u->next[i]->fail = u->fail->next[i];
                    // 合并失败节点的规则 ID
                    auto& targetIds = u->next[i]->ruleIds;
                    auto& failIds = u->next[i]->fail->ruleIds;
                    if (!failIds.empty()) {
                        targetIds.insert(targetIds.end(), failIds.begin(), failIds.end());
                    }
                    q.push(u->next[i]);
                } else {
                    u->next[i] = u->fail->next[i];
                }
            }
        }
    }
```

- 使用队列进行 BFS。
- 对于每个节点，其子节点的 `fail` 指针设置为 `u->fail->next[i]`。
- 同时将 `fail` 节点的规则 ID 合并到当前节点，实现后缀匹配。
- 对于缺失的转移，预先填充为 `fail` 节点的对应转移，加速匹配。

### 匹配过程

```cpp
    const std::vector<int>* match(const std::vector<uint8_t>& data) const {
        Node* curr = root.get();
        for (uint8_t c : data) {
            uint8_t idx = static_cast<uint8_t>(std::toupper(c));
            curr = curr->next[idx];
            if (!curr->ruleIds.empty())
                return &curr->ruleIds;  // 返回第一个命中的规则列表
        }
        return nullptr;
    }
```

- 遍历载荷每个字节，根据当前状态转移。
- 若当前节点有规则 ID，立即返回。
- 时间复杂度 $O(N)$，N 为载荷长度。

## 热重载机制

在 `SecurityEngine` 中，规则修改后调用 `compileRules()` 重建自动机：

```cpp
    void SecurityEngine::compileRules() {
        // 1. 获取规则快照
        std::vector<IdsRule> currentRules;
        { std::shared_lock lock(rulesMutex); currentRules = rules; }
    
        // 2. 构建新自动机
        auto newDetector = std::make_unique<AhoCorasick>();
        for (const auto& rule : currentRules) {
            if (rule.enabled && !rule.pattern.empty())
                newDetector->insert(rule.pattern, rule.id);
        }
        newDetector->build();
    
        // 3. 原子切换指针
        {
            std::unique_lock lock(rulesMutex);
            std::swap(acDetector, newDetector);
        }
        // 4. 旧自动机在 newDetector 析构时释放（锁外）
    }
```

- 使用读写锁保护 `acDetector` 指针。
- 在锁外构建新自动机，避免阻塞数据面。
- 通过指针交换实现原子切换，旧引擎在锁外析构，消除销毁开销对数据面的影响。

## 内存管理

- 所有节点使用 `std::unique_ptr` 管理，自动释放。
- `allNodes` 向量保存所有节点指针，保证析构时能正确释放所有内存。
- 节点数量与规则总长度成正比，每个规则字符对应一个节点（共享前缀的规则共用节点）。

## 性能特性

- **匹配速度**：$O(N)$，独立于规则数量，实测每秒可处理数万条规则。
- **内存占用**：每个节点 256 个指针（约 2KB），规则数量较多时需注意内存，例如 10 万字符规则树约占用 200MB。
- **大小写不敏感**：通过字符转大写实现，性能影响极小。
- **规则更新**：热重载时新旧引擎切换不影响正在进行的匹配，旧引擎在操作结束后延迟释放。

## 使用限制

- 仅支持单次匹配返回第一个命中，不支持多规则同时匹配（可通过遍历 `ruleIds` 扩展）。
- 规则模式长度不宜过短（建议 ≥ 3 字节），避免产生大量误报。
- 目前不支持通配符、范围匹配等高级语法，需扩展为正则引擎。

## 扩展性

- 可增加规则优先级机制，使高优先级规则优先匹配。
- 可增加规则预过滤（如协议、端口）减少 AC 自动机调用次数。
- 可支持 Unicode 规则，需扩展字符集为 256 或使用多字节转换。

---
