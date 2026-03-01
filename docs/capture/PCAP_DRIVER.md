# PCAP 驱动实现

## 概述

`PcapCapture` 是 Sentinel-Flow 默认的流量捕获驱动，基于 `libpcap` 库实现。它负责从物理网卡或离线文件获取原始网络报文，通过哈希分流将报文分发至多个无锁队列，并实现智能背压保护机制。

## 类设计

```cpp
    class PcapCapture : public sentinel::capture::ICaptureDriver {
    public:
        static PcapCapture& instance();              // 单例访问
    
        void init(const std::vector<PacketQueue*>& queues) override;
        void start(const std::string& device) override;
        void stop() override;
        bool setFilter(const std::string& filterExp) override;
        std::vector<std::string> getDeviceList() override;
    
    private:
        void captureLoop();                           // 捕获主循环
        int hashPacket(const uint8_t* data, int len, uint32_t offset);
    
        std::atomic<bool> running{false};
        std::thread captureThread;
        std::string currentDevice;
    
        std::vector<PacketQueue*> workerQueues;
        size_t queueCount = 0;
    
        mutable std::shared_mutex handleMutex;        // 保护 pcap_t 句柄
    };
```

## 初始化与启动

1. **初始化**：`init()` 接收工作队列指针列表，保存到 `workerQueues`。
2. **启动**：`start(device)` 设置设备名，启动独立线程 `captureLoop`。

## 捕获循环核心流程

```cpp
    void PcapCapture::captureLoop() {
        // 1. 打开设备
        handle = pcap_open_live(currentDevice.c_str(), BUFSIZ, 1, 1000, errbuf);
        // 2. 确定链路层偏移
        int dlt = pcap_datalink(handle);
        uint32_t linkOffset = (dlt == DLT_LINUX_SLL) ? 16 : 14;
    
        while (running) {
            // 3. 获取下一个报文
            int res = pcap_next_ex(handle, &header, &pkt_data);
            // 4. 哈希分流
            int workerId = hashPacket(pkt_data, caplen, linkOffset);
            auto* targetQueue = workerQueues[workerId];
    
            // 5. 背压控制
            bool isCongested = (targetQueue->size() > 5000);
            uint32_t copyLen = isCongested ? (linkOffset + 20 + 20 + 64) : caplen;
    
            // 6. 从内存池获取内存块
            MemoryBlock* rawBlock = PacketPool::instance().acquire();
            std::memcpy(rawBlock->data, pkt_data, copyLen);
            rawBlock->size = copyLen;
    
            // 7. 构造 RawPacket
            RawPacket raw;
            raw.kernelTimestampNs = header->ts.tv_sec * 1e9 + header->ts.tv_usec * 1000;
            raw.linkLayerOffset = linkOffset;
            raw.block = BlockPtr(rawBlock, BlockDeleter());
            raw.isTruncated = isCongested;
    
            // 8. 推入队列
            targetQueue->push(std::move(raw));
        }
    }
```

## 哈希分流算法

```cpp
    int PcapCapture::hashPacket(const uint8_t* data, int len, uint32_t offset) {
        if (len >= static_cast<int>(offset + 20)) {
            uint32_t saddr = *(uint32_t*)(data + offset + 12);  // 源 IP
            uint32_t daddr = *(uint32_t*)(data + offset + 16);  // 目的 IP
            return (saddr ^ daddr) % queueCount;                // 异或取模
        }
        return 0;
    }
```

- 使用源 IP 与目的 IP 的异或结果进行取模，保证同一会话的报文进入同一队列。
- 若报文长度不足以提取 IP 头部（如 ARP），则默认送入队列 0。

## 背压截断机制

当目标队列积压超过 **5000** 个报文时，触发截断：
- 仅拷贝头部数据（链路层 + IP 头 + TCP/UDP 头 + 64 字节余量）。
- 设置 `isTruncated = true` 标志，解析器将据此标记载荷被截断。
- 此机制防止内存池耗尽，并确保关键元数据（五元组、标志位）不丢失。

## BPF 过滤器

```cpp
    bool PcapCapture::setFilter(const std::string& filterExp) {
        std::unique_lock lock(handleMutex);
        if (!handle) return false;
    
        struct bpf_program fp;
        if (pcap_compile(handle, &fp, filterExp.c_str(), 1, PCAP_NETMASK_UNKNOWN) == -1)
            return false;
    
        bool success = (pcap_setfilter(handle, &fp) != -1);
        pcap_freecode(&fp);
        return success;
    }
```

- 通过 `pcap_compile` 编译 BPF 表达式，然后调用 `pcap_setfilter` 应用到当前会话。
- 使用 `std::shared_mutex` 保护句柄，支持运行时动态修改过滤器。
- 过滤器表达式在**内核层面**丢弃无关报文，极大减少用户态数据拷贝。

## 设备列表获取

```cpp
    std::vector<std::string> PcapCapture::getDeviceList() {
        std::vector<std::string> devs;
        pcap_if_t *alldevs, *d;
        if (pcap_findalldevs(&alldevs, errbuf) == 0) {
            for (d = alldevs; d; d = d->next)
                devs.push_back(d->name);
            pcap_freealldevs(alldevs);
        }
        return devs;
    }
```

返回系统所有可用网络接口名称（如 `eth0`, `wlan0`）。

## 线程安全

- `handleMutex` 用于保护 `pcap_t` 句柄，确保在设置过滤器或停止捕获时不会并发访问。
- 捕获线程与停止线程通过原子变量 `running` 同步，使用 `pcap_breakloop` 中断阻塞的 `pcap_next_ex`。

## 异常处理

- 若 `pcap_open_live` 失败，捕获线程直接退出，并输出错误信息。
- 若队列推入失败（队列满），输出警告并丢弃报文。

## 使用示例

```cpp
    // 获取驱动实例
    auto& driver = PcapCapture::instance();
    
    // 初始化队列
    std::vector<PacketQueue*> queues = { &queue1, &queue2 };
    driver.init(queues);
    
    // 启动捕获
    driver.start("eth0");
    
    // 设置 BPF 过滤器
    driver.setFilter("tcp port 80 or udp port 53");
    
    // 停止
    driver.stop();
```

## 性能建议

- **队列数量**：建议与 CPU 核心数匹配，通常设置为 `core_count - 2`。
- **混杂模式**：通过 `pcap_open_live` 的第三个参数启用（`1` 表示混杂模式）。
- **超时设置**：1000ms 的读取超时可保证在低流量时仍能及时退出。
- **BPF 优化**：尽量在过滤器中使用简单表达式，避免复杂逻辑影响内核性能。

---
