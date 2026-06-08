#pragma once

#include "sentinel/capture/ICapture.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <pcap.h>

// 前置声明
struct pcap;

namespace sentinel::capture {

// 基于 libpcap 的捕获驱动实现。
// 支持在线抓包和离线 pcap 文件读取两种模式。
// 线程安全：set_filter / stop 可并发调用，内部使用 shared_mutex 保护 pcap 句柄。
class PcapCapture final : public ICapture {
public:
    PcapCapture();
    ~PcapCapture() override;

    PcapCapture(const PcapCapture&) = delete;
    PcapCapture& operator=(const PcapCapture&) = delete;
    PcapCapture(PcapCapture&&) = delete;
    PcapCapture& operator=(PcapCapture&&) = delete;

    // ---- ICapture 接口 ----
    void set_packet_callback(PacketCallback cb) override;
    bool start(const std::string& device) override;
    void stop() override;
    bool set_filter(const std::string& expression) override;
    std::vector<std::string> list_devices() override;

    // ---- 模式控制 ----
    void set_offline_mode(bool offline) noexcept { offline_mode_ = offline; }
    void set_verbose(bool verbose) noexcept { verbose_ = verbose; }
    [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    void capture_loop();
    [[nodiscard]] int hash_packet(const uint8_t* data, size_t len, uint32_t offset) const noexcept;

    // 回调
    PacketCallback packet_cb_;

    // 运行状态
    std::atomic<bool> running_{false};
    std::thread capture_thread_;
    std::string device_;

    // pcap 句柄保护
    mutable std::shared_mutex handle_mutex_;
    pcap_t* handle_{nullptr};
    uint32_t link_offset_{14};

    // 模式
    bool offline_mode_{false};
    bool verbose_{false};
};

} // namespace sentinel::capture
