#pragma once

#include "sentinel/types/PacketTypes.h"

#include <atomic>
#include <optional>

namespace sentinel::flow {

// 协议解析开关 — 运行时可通过原子变量动态控制。
struct ParserConfig {
    std::atomic<bool> enable_tcp{true};
    std::atomic<bool> enable_udp{true};
    std::atomic<bool> enable_http{true};
    std::atomic<bool> enable_tls{true};
    std::atomic<bool> enable_icmp{true};
};

class PacketParser {
public:
    PacketParser() = delete;

    // 全局解析配置（线程安全读写）。
    static ParserConfig config;

    // 解析 RawPacket 为 ParsedPacket。
    // 返回 nullopt 表示报文格式无效（长度不足、IP 版本非 v4 等）。
    [[nodiscard]] static std::optional<types::ParsedPacket>
    parse(const types::RawPacket& raw);

private:
    // L3: IPv4 头部解析。span 起始于 IP 头部。
    // 成功返回 {ip_header_len, total_len, src_ip, dst_ip, ttl, protocol}。
    struct Ipv4Info {
        uint32_t header_len;
        uint32_t total_len;
        uint32_t src_ip;
        uint32_t dst_ip;
        uint8_t  ttl;
        uint8_t  protocol;
    };
    [[nodiscard]] static std::optional<Ipv4Info>
    parse_ipv4(std::span<const uint8_t> data);

    // L4: TCP 解析。span 起始于 TCP 头部。
    struct TcpInfo {
        uint16_t src_port;
        uint16_t dst_port;
        uint32_t header_len;
        std::string flags;
        std::span<const uint8_t> payload;
    };
    [[nodiscard]] static std::optional<TcpInfo>
    parse_tcp(std::span<const uint8_t> data);

    // L4: UDP 解析。span 起始于 UDP 头部。
    struct UdpInfo {
        uint16_t src_port;
        uint16_t dst_port;
        uint32_t length;
        std::span<const uint8_t> payload;
    };
    [[nodiscard]] static std::optional<UdpInfo>
    parse_udp(std::span<const uint8_t> data);

    // L7: 应用层协议识别与字段提取。
    static void parse_l7(types::ParsedPacket& pkt, std::span<const uint8_t> payload);
};

} // namespace sentinel::flow
