#pragma once
#include "common/memory/ObjectPool.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <array>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <string_view>
#include <arpa/inet.h>

constexpr size_t MAX_PACKET_SIZE = 2048;

struct MemoryBlock {
    uint8_t data[MAX_PACKET_SIZE];
    uint32_t size = 0;
};

class PacketPool {
public:
    static ObjectPool<MemoryBlock>& instance() {
        static ObjectPool<MemoryBlock> pool(20000);
        return pool;
    }
};

struct BlockDeleter {
    void operator()(MemoryBlock* block) const {
        if (!block) return;
        try {
            PacketPool::instance().release(block);
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] BlockDeleter caught exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[FATAL] BlockDeleter caught unknown exception" << std::endl;
        }
    }
};

using BlockPtr = std::shared_ptr<MemoryBlock>;

struct RawPacket {
    int64_t kernelTimestampNs;
    BlockPtr block;
    uint32_t linkLayerOffset = 14;
    bool isTruncated = false;

    RawPacket() : kernelTimestampNs(0), block(nullptr), linkLayerOffset(14), isTruncated(false) {}
};

using MacAddress = std::array<uint8_t, 6>;

struct ParsedPacket {
    uint64_t id = 0;
    int64_t timestamp = 0;

    uint32_t srcIp = 0;
    uint32_t dstIp = 0;
    uint16_t srcPort = 0;
    uint16_t dstPort = 0;
    char protocol[16] = {0};

    uint32_t length = 0;
    uint32_t totalLen = 0;

    using MacAddress = std::array<uint8_t, 6>;
    MacAddress srcMac = {0};
    MacAddress dstMac = {0};
    uint32_t linkLayerOffset = 14;

    std::string tcpFlags;
    uint8_t ttl = 0;

    std::vector<uint8_t> payloadData;
    size_t payloadSize = 0;

    uint16_t httpMethodLen = 0;
    uint16_t httpUriOffset = 0;
    uint16_t httpUriLen = 0;
    uint16_t tlsSniOffset = 0;
    uint16_t tlsSniLen = 0;

    BlockPtr block;
    bool isTruncated = false;

    // 使用标准的 inet_ntop 进行 IPv4 字符串转换
    std::string ipToString(uint32_t ip) const {
        char buf[INET_ADDRSTRLEN];
        struct in_addr addr;
        addr.s_addr = ip;
        inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
        return std::string(buf);
    }

    std::string getSrcIpStr() const { return ipToString(srcIp); }
    std::string getDstIpStr() const { return ipToString(dstIp); }

    std::string getSrcStr() const {
        return getSrcIpStr() + (srcPort ? ":" + std::to_string(srcPort) : "");
    }
    std::string getDstStr() const {
        return getDstIpStr() + (dstPort ? ":" + std::to_string(dstPort) : "");
    }

    std::string getMacStr(const MacAddress& mac) const {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(buf);
    }

    std::string_view getHttpMethod() const {
        if (httpMethodLen == 0 || payloadData.empty()) return {};
        return {reinterpret_cast<const char*>(payloadData.data()), httpMethodLen};
    }

    std::string_view getHttpUri() const {
        if (httpUriLen == 0 || httpUriOffset + httpUriLen > payloadData.size()) return {};
        return {reinterpret_cast<const char*>(payloadData.data() + httpUriOffset), httpUriLen};
    }

    std::string_view getTlsSni() const {
        if (tlsSniLen == 0 || tlsSniOffset + tlsSniLen > payloadData.size()) return {};
        return {reinterpret_cast<const char*>(payloadData.data() + tlsSniOffset), tlsSniLen};
    }

    std::string getSummary() const {
        std::stringstream ss;
        if (srcPort == 0 && dstPort == 0) {
            ss << protocol << " " << getSrcIpStr() << " -> " << getDstIpStr() << " Len=" << length;
        } else {
            ss << protocol << " " << getSrcIpStr() << ":" << srcPort << " -> " 
               << getDstIpStr() << ":" << dstPort;
            if (!tcpFlags.empty()) {
                ss << " [" << tcpFlags << "]";
            }
            ss << " Len=" << length;
        }

        if (httpUriLen > 0) {
            auto uri = getHttpUri();
            ss << " URI: " << std::string(uri.begin(), uri.end());
        } else if (tlsSniLen > 0) {
            auto sni = getTlsSni();
            ss << " SNI: " << std::string(sni.begin(), sni.end());
        }

        if (isTruncated) {
            ss << " [TRUNCATED]";
        }

        return ss.str();
    }
};

struct Alert {
    enum Level { Info, Low, Medium, High, Critical };
    uint64_t timestamp;
    Level level;
    uint32_t sourceIp;
    std::string description;
    std::string ruleName;
};
