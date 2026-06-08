#include "sentinel/engine/flow/PacketParser.h"

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <span>

namespace sentinel::flow {

// ---- 全局配置 ----
ParserConfig PacketParser::config{};

// ---- 内部辅助：从 span 安全构造 string_view ----
static std::string_view span_to_sv(std::span<const uint8_t> s) {
    if (s.empty())
        return {};
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

// ---- L3: IPv4 ----
std::optional<PacketParser::Ipv4Info>
PacketParser::parse_ipv4(std::span<const uint8_t> data) {
    if (data.size() < sizeof(struct iphdr))
        return std::nullopt;

    const auto* ip = reinterpret_cast<const struct iphdr*>(data.data());

    if (ip->version != 4)
        return std::nullopt;
    if (ip->ihl < 5)
        return std::nullopt;

    uint32_t const header_len = static_cast<uint32_t>(ip->ihl) * 4;
    uint32_t const total_len = ntohs(ip->tot_len);

    if (header_len < sizeof(struct iphdr) || total_len < header_len)
        return std::nullopt;
    if (data.size() < header_len)
        return std::nullopt;

    return Ipv4Info{
        .header_len = header_len,
        .total_len = total_len,
        .src_ip = ntohl(ip->saddr),
        .dst_ip = ntohl(ip->daddr),
        .ttl = ip->ttl,
        .protocol = ip->protocol,
    };
}

// ---- L4: TCP ----
std::optional<PacketParser::TcpInfo>
PacketParser::parse_tcp(std::span<const uint8_t> data) {
    if (data.size() < sizeof(struct tcphdr))
        return std::nullopt;

    const auto* tcp = reinterpret_cast<const struct tcphdr*>(data.data());
    uint32_t const header_len = static_cast<uint32_t>(tcp->th_off) * 4;

    if (header_len < sizeof(struct tcphdr) || data.size() < header_len)
        return std::nullopt;

    // 构造 TCP 标志字符串
    std::string flags;
    if (tcp->syn)
        flags += "SYN,";
    if (tcp->ack)
        flags += "ACK,";
    if (tcp->fin)
        flags += "FIN,";
    if (tcp->rst)
        flags += "RST,";
    if (tcp->psh)
        flags += "PSH,";
    if (tcp->urg)
        flags += "URG,";
    if (!flags.empty())
        flags.pop_back(); // 去除尾部逗号

    // 载荷 span：跳过 TCP 头部
    std::span<const uint8_t> payload;
    if (data.size() > header_len) {
        payload = data.subspan(header_len);
    }

    return TcpInfo{
        .src_port = ntohs(tcp->th_sport),
        .dst_port = ntohs(tcp->th_dport),
        .header_len = header_len,
        .flags = std::move(flags),
        .payload = payload,
    };
}

// ---- L4: UDP ----
std::optional<PacketParser::UdpInfo>
PacketParser::parse_udp(std::span<const uint8_t> data) {
    if (data.size() < sizeof(struct udphdr))
        return std::nullopt;

    const auto* udp = reinterpret_cast<const struct udphdr*>(data.data());
    uint16_t const datagram_len = ntohs(udp->uh_ulen);
    uint32_t const payload_len = (datagram_len > sizeof(struct udphdr))
        ? static_cast<uint32_t>(datagram_len - sizeof(struct udphdr))
        : 0;

    std::span<const uint8_t> payload;
    if (data.size() > sizeof(struct udphdr)) {
        size_t const actual = std::min<size_t>(payload_len, data.size() - sizeof(struct udphdr));
        payload = data.subspan(sizeof(struct udphdr), actual);
    }

    return UdpInfo{
        .src_port = ntohs(udp->uh_sport),
        .dst_port = ntohs(udp->uh_dport),
        .length = payload_len,
        .payload = payload,
    };
}

// ---- L7: 应用层识别 ----
void PacketParser::parse_l7(types::ParsedPacket& pkt, std::span<const uint8_t> payload) {
    if (payload.size() < 4)
        return;

    // --- HTTP 检测 ---
    if (config.enable_http.load(std::memory_order_relaxed)) {
        std::string_view const sv = span_to_sv(payload);

        if (sv.starts_with("GET ") || sv.starts_with("POST ") ||
            sv.starts_with("PUT ") || sv.starts_with("DELETE ") ||
            sv.starts_with("HEAD ") || sv.starts_with("HTTP/")) {

            pkt.protocol = "HTTP";

            if (!sv.starts_with("HTTP/")) {
                // 提取 HTTP Method
                size_t const space1 = sv.find(' ');
                if (space1 != std::string_view::npos) {
                    pkt.http_method = sv.substr(0, space1);
                    // 提取 URI
                    size_t const space2 = sv.find(' ', space1 + 1);
                    if (space2 != std::string_view::npos) {
                        pkt.http_uri = sv.substr(space1 + 1, space2 - space1 - 1);
                    }
                }
            }
            return;
        }
    }

    // --- TLS ClientHello 检测 ---
    if (config.enable_tls.load(std::memory_order_relaxed)) {
        if (payload[0] == 0x16 && payload[1] == 0x03 && payload.size() > 43 && payload[5] == 0x01) {

            pkt.protocol = "TLS";

            // 遍历 TLS 扩展提取 SNI
            size_t pos = 43;
            // 跳过 session_id
            if (pos < payload.size()) {
                pos += 1 + payload[pos];
            }
            // 跳过 cipher_suites
            if (pos + 2 <= payload.size()) {
                pos += 2 + (static_cast<size_t>(payload[pos]) << 8 | payload[pos + 1]);
            }
            // 跳过 compression_methods
            if (pos + 1 <= payload.size()) {
                pos += 1 + payload[pos];
            }

            // 扩展区
            if (pos + 2 <= payload.size()) {
                uint16_t const ext_total_len = static_cast<uint16_t>(payload[pos]) << 8 | payload[pos + 1];
                pos += 2;
                size_t const ext_end = std::min(pos + ext_total_len, payload.size());

                while (pos + 4 <= ext_end) {
                    uint16_t const ext_type = static_cast<uint16_t>(payload[pos]) << 8 | payload[pos + 1];
                    uint16_t const ext_len  = static_cast<uint16_t>(payload[pos + 2]) << 8 | payload[pos + 3];
                    pos += 4;
                    if (pos + ext_len > ext_end)
                        break;

                    if (ext_type == 0x0000) { // SNI 扩展
                        size_t sni_pos = pos;
                        if (sni_pos + 2 <= pos + ext_len) {
                            sni_pos += 2; // skip server_name_list length
                            if (sni_pos + 3 <= pos + ext_len) {
                                uint8_t  const name_type = payload[sni_pos];
                                uint16_t const name_len  = static_cast<uint16_t>(payload[sni_pos + 1]) << 8 | payload[sni_pos + 2];
                                sni_pos += 3;
                                if (name_type == 0 && sni_pos + name_len <= pos + ext_len) {
                                    pkt.tls_sni = span_to_sv(payload.subspan(sni_pos, name_len));
                                    break;
                                }
                            }
                        }
                    }
                    pos += ext_len;
                }
            }
            return;
        }
    }

    // --- 端口启发式回退 ---
    if (pkt.src_port == 80 || pkt.dst_port == 80) {
        if (config.enable_http.load(std::memory_order_relaxed))
            pkt.protocol = "HTTP";
    } else if (pkt.src_port == 443 || pkt.dst_port == 443) {
        if (config.enable_tls.load(std::memory_order_relaxed))
            pkt.protocol = "TLS";
    }
}

// ---- 主解析入口 ----
std::optional<types::ParsedPacket>
PacketParser::parse(const types::RawPacket& raw) {
    std::span<const uint8_t> const full = raw.payload();
    if (full.size() < raw.link_layer_offset + sizeof(struct iphdr))
        return std::nullopt;

    types::ParsedPacket pkt;

    // 报文 ID
    static std::atomic<uint32_t> s_thread_counter{0};
    thread_local uint32_t const t_thread_id = ++s_thread_counter;
    thread_local uint32_t t_packet_count = 0;
    pkt.id = (static_cast<uint64_t>(t_thread_id) << 32) | (++t_packet_count);

    pkt.timestamp_ms = raw.kernel_timestamp_ns / 1'000'000;
    pkt.block = raw.block;
    pkt.total_length = static_cast<uint32_t>(full.size());
    pkt.link_layer_offset = raw.link_layer_offset;
    pkt.truncated = raw.truncated;

    // MAC 地址提取
    if (raw.link_layer_offset >= 14 && full.size() >= 14) {
        std::memcpy(pkt.dst_mac.data(), full.data(), 6);
        std::memcpy(pkt.src_mac.data(), full.data() + 6, 6);
    }

    // L3: IPv4
    std::span<const uint8_t> const ip_span = full.subspan(raw.link_layer_offset);
    auto ip_info = parse_ipv4(ip_span);
    if (!ip_info)
        return std::nullopt;

    pkt.src_ip = ip_info->src_ip;
    pkt.dst_ip = ip_info->dst_ip;
    pkt.ttl = ip_info->ttl;

    // 约束有效载荷范围
    size_t const effective_len = std::min<size_t>(
        raw.link_layer_offset + ip_info->total_len, full.size());
    std::span<const uint8_t> const l4_span = full.subspan(
        raw.link_layer_offset + ip_info->header_len,
        (effective_len > raw.link_layer_offset + ip_info->header_len)
            ? effective_len - raw.link_layer_offset - ip_info->header_len
            : 0);

    // L4 分派
    switch (ip_info->protocol) {
    case IPPROTO_TCP: {
        if (!config.enable_tcp.load(std::memory_order_relaxed))
            return std::nullopt;

        auto tcp_info = parse_tcp(l4_span);
        if (!tcp_info)
            return std::nullopt;

        pkt.protocol = "TCP";
        pkt.src_port = tcp_info->src_port;
        pkt.dst_port = tcp_info->dst_port;
        pkt.tcp_flags = std::move(tcp_info->flags);
        pkt.payload_length = static_cast<uint32_t>(tcp_info->payload.size());

        if (!tcp_info->payload.empty()) {
            parse_l7(pkt, tcp_info->payload);
        }
        break;
    }
    case IPPROTO_UDP: {
        if (!config.enable_udp.load(std::memory_order_relaxed))
            return std::nullopt;

        auto udp_info = parse_udp(l4_span);
        if (!udp_info)
            return std::nullopt;

        pkt.protocol = "UDP";
        pkt.src_port = udp_info->src_port;
        pkt.dst_port = udp_info->dst_port;
        pkt.payload_length = udp_info->length;

        if (!udp_info->payload.empty()) {
            parse_l7(pkt, udp_info->payload);
        }
        break;
    }
    case IPPROTO_ICMP: {
        if (!config.enable_icmp.load(std::memory_order_relaxed))
            return std::nullopt;

        pkt.protocol = "ICMP";
        pkt.payload_length = static_cast<uint32_t>(l4_span.size());
        break;
    }
    default:
        pkt.protocol = "IPv4";
        pkt.payload_length = static_cast<uint32_t>(l4_span.size());
        break;
    }

    return pkt;
}

} // namespace sentinel::flow
