# 零拷贝内存池

## 概述

`ObjectPool` 是 Sentinel-Flow 实现零拷贝数据流的核心基础组件。它管理固定大小的 `MemoryBlock` 对象，提供无锁的分配与回收机制，确保数据包在捕获、解析、消费全链路中避免频繁的 `new`/`delete` 操作，从而消除内存碎片并提升性能。

## 设计目标

- **热路径零分配**：在数据包处理的核心循环中，禁止动态内存分配，所有内存从预分配池中借用。
- **无锁并发**：使用原子操作实现空闲链表，支持多线程安全地获取和释放对象。
- **内存局部性**：所有对象预分配在连续内存区域，提高缓存命中率。
- **生命周期托管**：通过自定义删除器与 `std::shared_ptr` 结合，自动归还内存到池中。

## 核心数据结构

### MemoryBlock

```cpp
    struct MemoryBlock {
        uint8_t data[MAX_PACKET_SIZE];   // 固定大小缓冲区 (2048 字节)
        uint32_t size = 0;               // 实际数据长度
    };
```

- `MAX_PACKET_SIZE = 2048` 可覆盖绝大多数网络报文（含以太网帧头）。
- 每个 `MemoryBlock` 由 `ObjectPool<MemoryBlock>` 管理。

### ObjectPool 节点结构

```cpp
    struct PoolBlock {
        PoolBlock* next = nullptr;          // 空闲链表指针
        bool constructed = false;            // 是否已构造对象
        alignas(T) std::byte storage[sizeof(T)];  // 对象存储区
    
        T* objectPtr() noexcept;
        void construct();
        void destroy();
        static PoolBlock* fromObjectPtr(T* obj) noexcept;
    };
```

- `PoolBlock` 将对象存储与链表节点融合，避免额外内存开销。
- `alignas(T)` 保证对象对齐。
- `fromObjectPtr` 通过指针偏移计算获取所属的 `PoolBlock`。

## 无锁空闲链表

空闲链表使用 **Tagged Pointer** 技术解决 ABA 问题：

```cpp
    struct TaggedHead {
        PoolBlock* ptr = nullptr;
        std::uint64_t tag = 0;   // 版本计数器
    };
    std::atomic<TaggedHead> freeHead_;
```

- 每次 CAS 操作时，同时比较指针和 tag。
- 若指针相同但 tag 不同，操作失败，防止 ABA 问题。

### 获取节点 (`popFree_`)

```cpp
    PoolBlock* popFree_() noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        while (head.ptr) {
            PoolBlock* node = head.ptr;
            TaggedHead next{node->next, head.tag + 1};
            if (freeHead_.compare_exchange_weak(head, next, 
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                node->next = nullptr;
                return node;
            }
        }
        return nullptr;
    }
```

- 如果空闲链表非空，取出头节点。
- 使用 CAS 更新头指针，成功则返回节点。

### 归还节点 (`pushFree_`)

```cpp
    void pushFree_(PoolBlock* node) noexcept {
        TaggedHead head = freeHead_.load(std::memory_order_acquire);
        for (;;) {
            node->next = head.ptr;
            TaggedHead next{node, head.tag + 1};
            if (freeHead_.compare_exchange_weak(head, next,
                    std::memory_order_release, std::memory_order_acquire))
                return;
        }
    }
```

- 将节点插入链表头部，同时增加 tag 版本。

## 预分配与动态扩容

```cpp
    ObjectPool(std::size_t preallocate) {
        reserve(preallocate);
    }
    
    void reserve(std::size_t n) {
        // 分配 n 个 PoolBlock 并构造对象
        // 依次插入空闲链表
    }
```

- 构造函数支持预分配数量，减少运行时分配。
- 若池中无可用对象，`acquire()` 会调用 `allocateOne_()` 动态分配新节点（使用 `std::lock_guard` 保护全局分配列表，避免多线程竞争分配）。

## 生命周期管理

### 获取对象

```cpp
    T* acquire() {
        PoolBlock* b = popFree_();
        if (b) {
            if (!b->constructed) b->construct();
            return b->objectPtr();
        }
        return allocateOne_()->objectPtr();
    }
```

- 优先从空闲链表获取，若为空则分配新节点。
- 若节点未构造，调用 `construct()` 执行 placement new。

### 释放对象

```cpp
    void release(T* obj) noexcept {
        if (!obj) return;
        PoolBlock* b = PoolBlock::fromObjectPtr(obj);
        pushFree_(b);
    }
```

- 通过 `fromObjectPtr` 获取所属节点，放回空闲链表。
- 对象本身不销毁，仅放回池中，避免析构开销。

### 智能指针包装

```cpp
    struct Deleter {
        ObjectPool* pool = nullptr;
        void operator()(T* p) const noexcept {
            if (pool && p) pool->release(p);
        }
    };
    using UniquePtr = std::unique_ptr<T, Deleter>;
    
    UniquePtr acquireUnique() {
        return UniquePtr(acquire(), Deleter{this});
    }
```

- 提供 `UniquePtr` 类型，自动调用 `release`。
- 实际代码中使用 `std::shared_ptr` 和自定义删除器 `BlockDeleter`。

## 在数据流中的使用

### 1. 捕获阶段

```cpp
    MemoryBlock* rawBlock = PacketPool::instance().acquire();
    std::memcpy(rawBlock->data, pkt_data, copyLen);
    rawBlock->size = copyLen;
    RawPacket raw;
    raw.block = BlockPtr(rawBlock, BlockDeleter());  // shared_ptr 管理
```

- `PacketPool` 是 `ObjectPool<MemoryBlock>` 的单例实例，预分配 20000 个块。
- 使用自定义删除器 `BlockDeleter` 调用 `PacketPool::release`。

### 2. 解析阶段

```cpp
    RawPacket raw = ...;
    auto parsed = PacketParser::parse(raw);
    // parsed->block 持有对同一 MemoryBlock 的 shared_ptr
```

- `ParsedPacket` 保留 `block` 成员，与 `RawPacket` 共享所有权。

### 3. 消费与释放

```cpp
    parsed.block.reset();   // 显式释放，或随着 ParsedPacket 析构自动释放
```

- 当最后一个持有者释放 `shared_ptr` 时，`BlockDeleter` 将 `MemoryBlock` 归还池中。
- 消费端（如取证模块、统计输出）使用完数据后释放引用，内存块自动回池。

## 内存回收与生命周期

- **捕获线程**：从池中获取块，填充数据，传递给队列。
- **解析线程**：从队列取出，解析后可能将块转移到 `ParsedPacket`。
- **消费端线程**：若 `ParsedPacket` 被保留（如暂存于取证缓冲区），块引用一直存在；当消费端完成处理后释放引用，块被归还池中。

## 性能特性

- **无锁操作**：`acquire`/`release` 在有空闲节点时完全无锁。
- **缓存友好**：节点预分配在连续内存（通过 `std::vector` 存储），链表操作只在头部进行，减少指针跳转。
- **零拷贝**：数据块从捕获到消费始终是同一块物理内存，无拷贝开销。

## 配置建议

- 默认池容量：20000 个 `MemoryBlock`（约 40 MB 内存）。
- 可根据网络吞吐量调整预分配数量：`PacketPool::instance().reserve(50000);`。

## 注意事项

- 若池满且无空闲节点，`acquire` 会动态分配新节点，此时可能进入临界区（`allocMutex_`）。因此在高吞吐场景下应保证预分配足够。
- 所有 `MemoryBlock` 的释放必须通过 `PacketPool::release`，不可直接 `delete`。

