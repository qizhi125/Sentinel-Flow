#pragma once
#include "common/memory/ObjectPool.h"
#include <string>
#include <vector>
#include <cstdint>
#include <QString>
#include <QMetaType>
#include <memory>
#include <array>
#include <iomanip>
#include <sstream>
#include <QHostAddress>
#include <iostream>
#include <string_view>

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

    QString getSrcIpStr() const { return QHostAddress(srcIp).toString(); }
    QString getDstIpStr() const { return QHostAddress(dstIp).toString(); }

    QString getSrcStr() const {
        return getSrcIpStr() + (srcPort ? ":" + QString::number(srcPort) : "");
    }
    QString getDstStr() const {
        return getDstIpStr() + (dstPort ? ":" + QString::number(dstPort) : "");
    }

    QString getMacStr(const MacAddress& mac) const {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return QString(buf);
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

    QString getSummary() const {
        QString base;
        if (srcPort == 0 && dstPort == 0) {
            base = QString("%1 %2 -> %3 Len=%4")
                   .arg(protocol, getSrcIpStr(), getDstIpStr(), QString::number(length));
        } else {
            base = QString("%1 %2 -> %3 %4 Len=%5")
                   .arg(protocol, QString::number(srcPort), QString::number(dstPort),
                        tcpFlags.empty() ? "" : "[" + QString::fromStdString(tcpFlags) + "]",
                        QString::number(length));
        }

        if (httpUriLen > 0) {
            auto uri = getHttpUri();
            base += " URI: " + QString::fromUtf8(uri.data(), uri.size());
        } else if (tlsSniLen > 0) {
            auto sni = getTlsSni();
            base += " SNI: " + QString::fromUtf8(sni.data(), sni.size());
        }

        if (isTruncated) {
            base += " [TRUNCATED]";
        }

        return base;
    }
};

Q_DECLARE_METATYPE(ParsedPacket)
Q_DECLARE_METATYPE(QVector<ParsedPacket>)

struct Alert {
    enum Level { Info, Low, Medium, High, Critical };
    uint64_t timestamp;
    Level level;
    uint32_t sourceIp;
    std::string description;
    std::string ruleName;
};
Q_DECLARE_METATYPE(Alert)