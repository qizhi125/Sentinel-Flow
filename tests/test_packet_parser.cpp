#include <gtest/gtest.h>
#include "sentinel/engine/flow/PacketParser.h"
#include "sentinel/types/PacketTypes.h"

#include <cstring>
#include <netinet/in.h>

using namespace sentinel::flow;
using namespace sentinel::types;

// ---- 辅助：构造最小 IPv4 + TCP 报文 ----
static RawPacket make_ipv4_tcp_packet(uint16_t src_port, uint16_t dst_port,
                                       const uint8_t* payload, size_t payload_len,
                                       bool syn = true, bool ack = false) {
    // 手工构造: 以太网头(14) + IP头(20) + TCP头(20) + payload
    constexpr size_t kEthHdr = 14;
    constexpr size_t kIpHdr  = 20;
    constexpr size_t kTcpHdr = 20;
    size_t const total_size = kEthHdr + kIpHdr + kTcpHdr + payload_len;

    auto block = std::make_shared<MemoryBlock>();
    block->size = static_cast<uint32_t>(total_size);
    std::memset(block->data, 0, total_size);

    // 链路层偏移 (跳过 MAC 地址)
    uint8_t* p = block->data + kEthHdr;

    // IP 头部
    p[0] = 0x45;            // version=4, ihl=5
    p[2] = static_cast<uint8_t>((total_size - kEthHdr) >> 8);   // total_len hi
    p[3] = static_cast<uint8_t>((total_size - kEthHdr) & 0xFF); // total_len lo
    p[8] = 64;              // ttl
    p[9] = IPPROTO_TCP;     // protocol
    p[12] = 192; p[13] = 168; p[14] = 1; p[15] = 1;   // src_ip = 192.168.1.1
    p[16] = 10;  p[17] = 0;  p[18] = 0; p[19] = 2;    // dst_ip = 10.0.0.2

    p += kIpHdr;

    // TCP 头部
    p[0] = static_cast<uint8_t>(src_port >> 8);       // src_port hi
    p[1] = static_cast<uint8_t>(src_port & 0xFF);     // src_port lo
    p[2] = static_cast<uint8_t>(dst_port >> 8);       // dst_port hi
    p[3] = static_cast<uint8_t>(dst_port & 0xFF);     // dst_port lo
    p[12] = 0x50;                                      // data offset = 5 (20 bytes)
    if (syn) p[13] |= 0x02;                            // SYN flag
    if (ack) p[13] |= 0x10;                            // ACK flag

    p += kTcpHdr;

    // 载荷
    if (payload && payload_len > 0) {
        std::memcpy(p, payload, payload_len);
    }

    RawPacket raw;
    raw.kernel_timestamp_ns = 1'000'000'000;
    raw.block = block;
    raw.link_layer_offset = kEthHdr;

    return raw;
}

// ---- 基本 TCP 解析 ----
TEST(PacketParserTest, ParseTcpBasic) {
    auto raw = make_ipv4_tcp_packet(12345, 80, nullptr, 0, true, true);

    auto result = PacketParser::parse(raw);
    ASSERT_TRUE(result.has_value());

    auto& pkt = *result;
    EXPECT_EQ(pkt.src_ip, 0xc0a80101u);   // 192.168.1.1
    EXPECT_EQ(pkt.dst_ip, 0x0a000002u);    // 10.0.0.2
    EXPECT_EQ(pkt.src_port, 12345);
    EXPECT_EQ(pkt.dst_port, 80);
    EXPECT_EQ(pkt.protocol, "TCP");
    EXPECT_TRUE(pkt.tcp_flags.find("SYN") != std::string::npos);
    EXPECT_TRUE(pkt.tcp_flags.find("ACK") != std::string::npos);
    EXPECT_EQ(pkt.ttl, 64);
}

// ---- UDP 解析 ----
TEST(PacketParserTest, ParseUdpBasic) {
    constexpr size_t kEthHdr = 14;
    constexpr size_t kIpHdr  = 20;
    constexpr size_t kUdpHdr = 8;
    size_t const total_size = kEthHdr + kIpHdr + kUdpHdr;

    auto block = std::make_shared<MemoryBlock>();
    block->size = static_cast<uint32_t>(total_size);
    std::memset(block->data, 0, total_size);

    uint8_t* p = block->data + kEthHdr;

    // IP 头部
    p[0] = 0x45;
    p[2] = static_cast<uint8_t>((total_size - kEthHdr) >> 8);
    p[3] = static_cast<uint8_t>((total_size - kEthHdr) & 0xFF);
    p[8] = 64;
    p[9] = IPPROTO_UDP;
    p[12] = 10;  p[13] = 0;  p[14] = 0; p[15] = 1;
    p[16] = 10;  p[17] = 0;  p[18] = 0; p[19] = 2;

    p += kIpHdr;

    // UDP 头部
    p[0] = static_cast<uint8_t>(53 >> 8);    // src_port = 53 (DNS)
    p[1] = static_cast<uint8_t>(53 & 0xFF);
    p[2] = static_cast<uint8_t>(12345 >> 8);  // dst_port
    p[3] = static_cast<uint8_t>(12345 & 0xFF);
    p[4] = 0x00;                              // len = 8 (header only)
    p[5] = 0x08;

    RawPacket raw;
    raw.kernel_timestamp_ns = 2'000'000'000;
    raw.block = block;
    raw.link_layer_offset = kEthHdr;

    auto result = PacketParser::parse(raw);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->protocol, "UDP");
    EXPECT_EQ(result->src_port, 53);
    EXPECT_EQ(result->dst_port, 12345);
}

// ---- HTTP 检测 ----
TEST(PacketParserTest, ParseHttpDetection) {
    const char* http_req = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    auto raw = make_ipv4_tcp_packet(34567, 80,
                                     reinterpret_cast<const uint8_t*>(http_req),
                                     std::strlen(http_req));

    auto result = PacketParser::parse(raw);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->protocol, "HTTP");
    EXPECT_EQ(result->http_method, "GET");
    EXPECT_EQ(result->http_uri, "/index.html");
}

// ---- TLS ClientHello SNI 检测 ----
TEST(PacketParserTest, ParseTlsSniDetection) {
    // 构造最小 TLS ClientHello 包含 SNI = "example.com"
    constexpr size_t kPayloadLen = 80;
    uint8_t payload[kPayloadLen] = {0};
    payload[0] = 0x16;            // handshake
    payload[1] = 0x03; payload[2] = 0x03; // TLS 1.2
    payload[5] = 0x01;            // client_hello
    // 填充 session_id_len = 0, cipher_suites_len = 0, compression_len = 0
    payload[43] = 0x00;                              // session_id_len
    payload[44] = 0x00; payload[45] = 0x02;         // cipher_suites len = 2
    payload[46] = 0x00; payload[47] = 0x00;         // cipher suite placeholder
    payload[48] = 0x01; payload[49] = 0x00;         // compression len = 1, method = 0
    // 扩展区
    payload[50] = 0x00; payload[51] = 18;           // ext total len = 17
    payload[52] = 0x00; payload[53] = 0x00;         // ext_type = SNI (0)
    payload[54] = 0x00; payload[55] = 14;           // ext_len = 13
    payload[56] = 0x00; payload[57] = 12;            // server_name_list len = 11
    payload[58] = 0x00;                               // name_type = host_name
    payload[59] = 0x00; payload[60] = 9;             // name_len = 8
    // "example" (7 bytes) + ".com" omitted for brevity — use short name
    const char* sni = "test.host";
    size_t sni_len = std::strlen(sni);
    payload[59] = 0x00;
    payload[60] = static_cast<uint8_t>(sni_len);
    std::memcpy(payload + 61, sni, sni_len);

    auto raw = make_ipv4_tcp_packet(50000, 443, payload, kPayloadLen);

    auto result = PacketParser::parse(raw);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->protocol, "TLS");
    EXPECT_EQ(result->tls_sni, "test.host");
}

// ---- 截断报文 ----
TEST(PacketParserTest, TruncatedPacket) {
    RawPacket raw;
    raw.block = std::make_shared<MemoryBlock>();
    raw.block->size = 10; // 远小于最小报文长度
    raw.link_layer_offset = 14;

    auto result = PacketParser::parse(raw);
    EXPECT_FALSE(result.has_value());
}

// ---- 非 IPv4 报文 ----
TEST(PacketParserTest, NonIpv4Rejected) {
    RawPacket raw;
    raw.block = std::make_shared<MemoryBlock>();
    raw.block->size = 100;
    raw.link_layer_offset = 14;
    // data[14] = 0 意味着 version=0（非 IPv4）
    std::memset(raw.block->data + 14, 0, 20);

    auto result = PacketParser::parse(raw);
    EXPECT_FALSE(result.has_value());
}

// ---- 协议开关：禁用 TCP ----
TEST(PacketParserTest, DisableTcp) {
    auto raw = make_ipv4_tcp_packet(12345, 80, nullptr, 0);

    PacketParser::config.enable_tcp.store(false, std::memory_order_relaxed);
    auto result = PacketParser::parse(raw);
    EXPECT_FALSE(result.has_value());

    PacketParser::config.enable_tcp.store(true, std::memory_order_relaxed); // 恢复
}
