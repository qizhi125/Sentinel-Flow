#include "PacketParser.h"
#include <sstream>
#include <iomanip>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <cstring>
#include <QDateTime>

static void copyStr(char* dest, const char* src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

std::atomic<bool> PacketParser::ENABLE_TCP{true};
std::atomic<bool> PacketParser::ENABLE_UDP{true};
std::atomic<bool> PacketParser::ENABLE_HTTP{true};
std::atomic<bool> PacketParser::ENABLE_TLS{true};
std::atomic<bool> PacketParser::ENABLE_ICMP{true};

std::optional<ParsedPacket> PacketParser::parse(const RawPacket& raw) {
    if (!raw.block || raw.block->size < 14) return std::nullopt;

    const uint8_t* data = raw.block->data;
    uint32_t length = raw.block->size;

    ParsedPacket pkt;
    static uint64_t globalId = 0;
    pkt.id = ++globalId;

    pkt.timestamp = raw.kernelTimestampNs / 1000000;

    pkt.totalLen = length;

    std::memcpy(pkt.dstMac.data(), data, 6);
    std::memcpy(pkt.srcMac.data(), data + 6, 6);

    uint16_t etherType = (data[12] << 8) | data[13];

    if (etherType != 0x0800) {
        copyStr(pkt.protocol, "ETH", sizeof(pkt.protocol));
        return pkt;
    }

    if (length < 34) return std::nullopt;

    // 解析 IP 头
    const struct iphdr* ipHeader = (struct iphdr*)(data + 14);

    pkt.srcIp = ntohl(ipHeader->saddr);
    pkt.dstIp = ntohl(ipHeader->daddr);
    pkt.ttl = ipHeader->ttl;

    size_t ipHeaderLen = ipHeader->ihl * 4;
    size_t protocolOffset = 14 + ipHeaderLen;

    if (ipHeader->protocol == IPPROTO_TCP) {
        copyStr(pkt.protocol, "TCP", sizeof(pkt.protocol));
        if (length < protocolOffset + 20) return std::nullopt;

        const struct tcphdr* tcpHeader = (struct tcphdr*)(data + protocolOffset);
        pkt.srcPort = ntohs(tcpHeader->source);
        pkt.dstPort = ntohs(tcpHeader->dest);

        // Flags 简单处理 (也可以优化为 bitmap)
        if (tcpHeader->syn) pkt.tcpFlags += "SYN ";
        if (tcpHeader->ack) pkt.tcpFlags += "ACK ";
        if (tcpHeader->fin) pkt.tcpFlags += "FIN ";
        if (tcpHeader->rst) pkt.tcpFlags += "RST ";
        if (tcpHeader->psh) pkt.tcpFlags += "PSH ";

        size_t tcpHeaderLen = tcpHeader->doff * 4;
        size_t payloadOffset = protocolOffset + tcpHeaderLen;
        pkt.length = (length > payloadOffset) ? (length - payloadOffset) : 0;

        if (pkt.length > 0) {
            pkt.payloadData.assign(data + payloadOffset, data + length);
            pkt.payloadSize = pkt.length;
        }

        // 端口识别
        if (pkt.srcPort == 80 || pkt.dstPort == 80) {
            if (ENABLE_HTTP) copyStr(pkt.protocol, "HTTP", sizeof(pkt.protocol));
        } else if (pkt.srcPort == 443 || pkt.dstPort == 443) {
            if (ENABLE_TLS) copyStr(pkt.protocol, "TLS", sizeof(pkt.protocol));
        }

    } else if (ipHeader->protocol == IPPROTO_UDP) {
        copyStr(pkt.protocol, "UDP", sizeof(pkt.protocol));
        if (length < protocolOffset + 8) return std::nullopt;

        const struct udphdr* udpHeader = (struct udphdr*)(data + protocolOffset);
        pkt.srcPort = ntohs(udpHeader->source);
        pkt.dstPort = ntohs(udpHeader->dest);
        pkt.length = ntohs(udpHeader->len) - 8;

        size_t payloadOffset = protocolOffset + 8;
        if (length > payloadOffset) {
            pkt.payloadData.assign(data + payloadOffset, data + length);
            pkt.payloadSize = pkt.payloadData.size();
        }

    } else if (ipHeader->protocol == IPPROTO_ICMP) {
        copyStr(pkt.protocol, "ICMP", sizeof(pkt.protocol));
        pkt.srcPort = 0;
        pkt.dstPort = 0;
    } else {
        copyStr(pkt.protocol, "IP", sizeof(pkt.protocol));
    }

    return pkt;
}