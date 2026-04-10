#include "PacketParser.h"
#include <sstream>
#include <iomanip>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <cstring>
#include <chrono>
#include <atomic>

static void copyStr(char* dest, const char* src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

std::atomic<bool> PacketParser::ENABLE_TCP{true};
std::atomic<bool> PacketParser::ENABLE_UDP{true};
std::atomic<bool> PacketParser::ENABLE_HTTP{true};
std::atomic<bool> PacketParser::ENABLE_TLS{true};
std::atomic<bool> PacketParser::ENABLE_ICMP{true};

static void parseL7(ParsedPacket& pkt) {
    if (pkt.payloadSize < 4) return;
    const uint8_t* data = pkt.payloadData.data();
    size_t len = pkt.payloadSize;

    if (PacketParser::ENABLE_HTTP) {
        std::string_view sv(reinterpret_cast<const char*>(data), len);
        if (sv.starts_with("GET ") || sv.starts_with("POST ") || sv.starts_with("PUT ") ||
            sv.starts_with("DELETE ") || sv.starts_with("HEAD ") || sv.starts_with("HTTP/")) {

            copyStr(pkt.protocol, "HTTP", sizeof(pkt.protocol));

            if (!sv.starts_with("HTTP/")) {
                size_t space1 = sv.find(' ');
                if (space1 != std::string_view::npos && space1 < std::numeric_limits<uint16_t>::max()) {
                    pkt.httpMethodLen = static_cast<uint16_t>(space1);
                    size_t space2 = sv.find(' ', space1 + 1);
                    if (space2 != std::string_view::npos) {
                        size_t uriLen = space2 - (space1 + 1);
                        if (space1 + 1 < std::numeric_limits<uint16_t>::max() && uriLen < std::numeric_limits<uint16_t>::max()) {
                            pkt.httpUriOffset = static_cast<uint16_t>(space1 + 1);
                            pkt.httpUriLen = static_cast<uint16_t>(uriLen);
                        }
                    }
                }
            }
            return;
        }
    }

    if (PacketParser::ENABLE_TLS) {
        if (data[0] == 0x16 && data[1] == 0x03 && len > 43 && data[5] == 0x01) {
            copyStr(pkt.protocol, "TLS", sizeof(pkt.protocol));

            size_t pos = 43;
            if (pos < len) { pos += 1 + data[pos]; }
            if (pos + 2 <= len) { pos += 2 + ((data[pos] << 8) | data[pos+1]); }
            if (pos + 1 <= len) { pos += 1 + data[pos]; }

            if (pos + 2 <= len) {
                uint16_t extTotalLen = (data[pos] << 8) | data[pos+1];
                pos += 2;
                size_t extEnd = std::min(pos + extTotalLen, len);

                while (pos + 4 <= extEnd) {
                    uint16_t extType = (data[pos] << 8) | data[pos+1];
                    uint16_t extLen = (data[pos+2] << 8) | data[pos+3];
                    pos += 4;
                    if (pos + extLen > extEnd) break;

                    if (extType == 0x0000) {
                        size_t sniPos = pos;
                        if (sniPos + 2 <= pos + extLen) {
                            sniPos += 2;
                            if (sniPos + 3 <= pos + extLen) {
                                uint8_t nameType = data[sniPos];
                                uint16_t nameLen = (data[sniPos+1] << 8) | data[sniPos+2];
                                sniPos += 3;
                                if (nameType == 0 && sniPos + nameLen <= pos + extLen) {
                                    if (sniPos < std::numeric_limits<uint16_t>::max() && nameLen < std::numeric_limits<uint16_t>::max()) {
                                        pkt.tlsSniOffset = static_cast<uint16_t>(sniPos);
                                        pkt.tlsSniLen = static_cast<uint16_t>(nameLen);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    pos += extLen;
                }
            }
            return;
        }
    }

    if (pkt.srcPort == 80 || pkt.dstPort == 80) {
        if (PacketParser::ENABLE_HTTP) copyStr(pkt.protocol, "HTTP", sizeof(pkt.protocol));
    } else if (pkt.srcPort == 443 || pkt.dstPort == 443) {
        if (PacketParser::ENABLE_TLS) copyStr(pkt.protocol, "TLS", sizeof(pkt.protocol));
    }
}

std::optional<ParsedPacket> PacketParser::parse(const RawPacket& raw) {
    if (!raw.block || raw.block->size < 14) return std::nullopt;

    const uint8_t* data = raw.block->data;
    uint32_t length = raw.block->size;
    uint32_t offset = raw.linkLayerOffset;

    if (length < offset + 20) return std::nullopt;

    ParsedPacket pkt;
    // 关键修复 3：使用原子变量消除多线程竞态
    static std::atomic<uint64_t> globalId{0};
    pkt.id = ++globalId;
    pkt.timestamp = raw.kernelTimestampNs / 1000000;
    pkt.block = raw.block;
    pkt.totalLen = length;
    pkt.linkLayerOffset = offset;
    pkt.isTruncated = raw.isTruncated;

    if (offset >= 14) {
        std::memcpy(pkt.dstMac.data(), data, 6);
        std::memcpy(pkt.srcMac.data(), data + 6, 6);
    }

    const struct iphdr* ipHeader = (struct iphdr*)(data + offset);
    if (ipHeader->version != 4) return std::nullopt;

    uint32_t ipTotalLen = ntohs(ipHeader->tot_len);
    if (offset + ipTotalLen < length) {
        length = offset + ipTotalLen;
    }

    pkt.srcIp = ntohl(ipHeader->saddr);
    pkt.dstIp = ntohl(ipHeader->daddr);
    pkt.ttl = ipHeader->ttl;

    uint32_t protocolOffset = offset + ipHeader->ihl * 4;
    if (length < protocolOffset) return std::nullopt;

    if (ipHeader->protocol == IPPROTO_TCP) {
        copyStr(pkt.protocol, "TCP", sizeof(pkt.protocol));
        if (length < protocolOffset + 20) return std::nullopt;

        const struct tcphdr* tcpHeader = (struct tcphdr*)(data + protocolOffset);
        pkt.srcPort = ntohs(tcpHeader->source);
        pkt.dstPort = ntohs(tcpHeader->dest);

        std::string flags;
        if (tcpHeader->syn) flags += "SYN,";
        if (tcpHeader->ack) flags += "ACK,";
        if (tcpHeader->fin) flags += "FIN,";
        if (tcpHeader->rst) flags += "RST,";
        if (tcpHeader->psh) flags += "PSH,";
        if (tcpHeader->urg) flags += "URG,";
        if (!flags.empty()) flags.pop_back();
        pkt.tcpFlags = flags;

        uint32_t tcpHeaderLen = tcpHeader->doff * 4;
        size_t payloadOffset = protocolOffset + tcpHeaderLen;
        pkt.length = (length > payloadOffset) ? (length - payloadOffset) : 0;

        if (pkt.length > 0) {
            pkt.payloadData.assign(data + payloadOffset, data + payloadOffset + pkt.length);
            pkt.payloadSize = pkt.length;
            parseL7(pkt);
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
            uint32_t actualPayloadLen = std::min<uint32_t>(pkt.length, length - payloadOffset);
            pkt.payloadData.assign(data + payloadOffset, data + payloadOffset + actualPayloadLen);
            pkt.payloadSize = actualPayloadLen;
            parseL7(pkt);
        }

    } else if (ipHeader->protocol == IPPROTO_ICMP) {
        copyStr(pkt.protocol, "ICMP", sizeof(pkt.protocol));
        pkt.length = length - protocolOffset;
        if (pkt.length > 0) {
            pkt.payloadData.assign(data + protocolOffset, data + length);
            pkt.payloadSize = pkt.length;
        }
    } else {
        copyStr(pkt.protocol, "IPv4", sizeof(pkt.protocol));
        pkt.length = length - protocolOffset;
    }

    return pkt;
}

std::string PacketParser::ipToString(uint32_t ip) {
    struct in_addr ip_addr;
    ip_addr.s_addr = htonl(ip);
    return std::string(inet_ntoa(ip_addr));
}