#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::types {

constexpr size_t kMaxPacketSize = 2048;

// 预分配内存块，由对象池管理。
struct MemoryBlock {
    uint8_t data[kMaxPacketSize];
    uint32_t size = 0;
};

using BlockPtr = std::shared_ptr<MemoryBlock>;

// 捕获层产出的原始报文。
// data 是 block->data 的 span 视图，零拷贝引用底层 MemoryBlock。
// block 持有所有权，确保 span 在解析期间有效。
struct RawPacket {
    int64_t kernel_timestamp_ns = 0;
    BlockPtr block;
    uint32_t link_layer_offset = 14;
    bool truncated = false;

    // 返回报文数据的零拷贝 span 视图。
    [[nodiscard]] std::span<const uint8_t> payload() const noexcept {
        if (!block || block->size == 0)
            return {};
        return {block->data, block->size};
    }
};

// 解析后的结构化报文。
struct ParsedPacket {
    uint64_t id = 0;
    int64_t timestamp_ms = 0;

    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    std::string protocol; // "TCP" / "UDP" / "ICMP" / "HTTP" / "TLS" / "IPv4"

    uint32_t payload_length = 0;
    uint32_t total_length = 0;

    std::array<uint8_t, 6> src_mac{};
    std::array<uint8_t, 6> dst_mac{};
    uint32_t link_layer_offset = 14;

    std::string tcp_flags;
    uint8_t ttl = 0;

    // L7 解析结果 — span 指向 block 内部数据，零拷贝
    std::string_view http_method;
    std::string_view http_uri;
    std::string_view tls_sni;

    BlockPtr block; // 持有数据所有权
    bool truncated = false;
};

// 解析结果：成功时包含 ParsedPacket，失败时包含错误描述。
struct ParseResult {
    ParsedPacket packet;
    std::string error; // 空表示成功
};

} // namespace sentinel::types
