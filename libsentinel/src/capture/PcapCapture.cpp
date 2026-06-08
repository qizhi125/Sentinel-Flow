#include "sentinel/capture/PcapCapture.h"

#include <pcap.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <shared_mutex>

namespace sentinel::capture {

// ---- 构造 / 析构 ----

PcapCapture::PcapCapture() = default;

PcapCapture::~PcapCapture() {
    stop();
}

// ---- 回调注册 ----

void PcapCapture::set_packet_callback(PacketCallback cb) {
    packet_cb_ = std::move(cb);
}

// ---- 设备枚举 ----

std::vector<std::string> PcapCapture::list_devices() {
    std::vector<std::string> devices;
    pcap_if_t* alldevs = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == 0) {
        for (pcap_if_t* d = alldevs; d != nullptr; d = d->next) {
            devices.emplace_back(d->name);
        }
        pcap_freealldevs(alldevs);
    }
    return devices;
}

// ---- 启动 / 停止 ----

bool PcapCapture::start(const std::string& device) {
    if (running_.load(std::memory_order_acquire)) {
        return true; // 已在运行
    }
    device_ = device;

    char errbuf[PCAP_ERRBUF_SIZE];
    {
        std::unique_lock lock(handle_mutex_);

        if (handle_) {
            pcap_close(handle_);
            handle_ = nullptr;
        }

        if (offline_mode_) {
            handle_ = pcap_open_offline(device_.c_str(), errbuf);
        } else {
            handle_ = pcap_open_live(device_.c_str(), 2048, 1, 10, errbuf);
        }
    }

    if (!handle_) {
        std::cerr << "[PcapCapture] 打开失败: " << errbuf << std::endl;
        return false;
    }

    // 确定链路层偏移
    {
        std::shared_lock lock(handle_mutex_);
        int const dlt = pcap_datalink(handle_);
        if (dlt == DLT_LINUX_SLL) {
            link_offset_ = 16;
        } else if (dlt == DLT_NULL || dlt == DLT_LOOP) {
            link_offset_ = 4;
        } else {
            link_offset_ = 14; // 以太网默认
        }
    }

    if (verbose_) {
        std::cout << "[PcapCapture] "
                  << (offline_mode_ ? "离线分析: " : "监听: ")
                  << device_ << std::endl;
    }

    running_.store(true, std::memory_order_release);
    capture_thread_ = std::thread(&PcapCapture::capture_loop, this);
    return true;
}

void PcapCapture::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);

    // 中断阻塞的 pcap_next_ex
    {
        std::shared_lock lock(handle_mutex_);
        if (handle_) {
            pcap_breakloop(handle_);
        }
    }

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    // 安全关闭句柄
    {
        std::unique_lock lock(handle_mutex_);
        if (handle_) {
            pcap_close(handle_);
            handle_ = nullptr;
        }
    }
}

// ---- 过滤器 ----

bool PcapCapture::set_filter(const std::string& expression) {
    std::unique_lock lock(handle_mutex_);
    if (!handle_) {
        return false;
    }

    struct bpf_program prog;
    if (pcap_compile(handle_, &prog, expression.c_str(), 1, PCAP_NETMASK_UNKNOWN) == -1) {
        return false;
    }

    bool const ok = (pcap_setfilter(handle_, &prog) != -1);
    pcap_freecode(&prog);
    return ok;
}

// ---- 五元组哈希 ----

int PcapCapture::hash_packet(const uint8_t* data, size_t len, uint32_t offset) const noexcept {
    if (len < offset + 20) {
        return 0;
    }
    uint32_t saddr = 0;
    uint32_t daddr = 0;
    std::memcpy(&saddr, data + offset + 12, 4);
    std::memcpy(&daddr, data + offset + 16, 4);
    return static_cast<int>(saddr ^ daddr);
}

// ---- 捕获主循环 ----

void PcapCapture::capture_loop() {
    // 安全获取句柄副本（shared_lock 允许并发 set_filter）
    pcap_t* local_handle = nullptr;
    {
        std::shared_lock lock(handle_mutex_);
        local_handle = handle_;
    }

    if (!local_handle) {
        running_.store(false, std::memory_order_release);
        return;
    }

    uint32_t const link_offset = link_offset_;

    struct pcap_pkthdr* header = nullptr;
    const u_char* pkt_data = nullptr;

    while (running_.load(std::memory_order_acquire)) {
        int const res = pcap_next_ex(local_handle, &header, &pkt_data);

        if (res == -2) {
            // 离线文件 EOF
            if (verbose_) {
                std::cout << "[PcapCapture] 离线 PCAP 读取完毕 (EOF)" << std::endl;
            }
            break;
        }
        if (res <= 0) {
            continue; // 超时或错误
        }

        // 构造 RawPacket — 当前实现需要拷贝数据。
        // 后续可集成 ObjectPool 实现零拷贝。
        uint32_t const caplen = std::min<uint32_t>(header->caplen, types::kMaxPacketSize);

        auto block = std::make_shared<types::MemoryBlock>();
        block->size = caplen;
        std::memcpy(block->data, pkt_data, caplen);

        types::RawPacket raw;
        raw.kernel_timestamp_ns = static_cast<int64_t>(header->ts.tv_sec) * 1'000'000'000L
                                + static_cast<int64_t>(header->ts.tv_usec) * 1'000L;
        raw.link_layer_offset = link_offset;
        raw.block = std::move(block);

        if (packet_cb_) {
            packet_cb_(std::move(raw));
        }
    }

    running_.store(false, std::memory_order_release);
}

} // namespace sentinel::capture
