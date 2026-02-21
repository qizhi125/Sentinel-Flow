#pragma once
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
#include "common/memory/ObjectPool.h"

constexpr size_t MAX_PACKET_SIZE = 2048;

struct MemoryBlock {
    uint8_t data[MAX_PACKET_SIZE];
    uint32_t size = 0; // 实际数据长度
};

class PacketPool {
public:
    static ObjectPool<MemoryBlock>& instance() {
        // 预分配 20,000 个块 (约 40MB 内存)，避免运行时分配
        static ObjectPool<MemoryBlock> pool(20000);
        return pool;
    }
};

struct BlockDeleter {
    void operator()(MemoryBlock* block) const {
        PacketPool::instance().release(block);
    }
};

using BlockPtr = std::shared_ptr<MemoryBlock>;

struct RawPacket {
    int64_t kernelTimestampNs;
    BlockPtr block;

    uint32_t linkLayerOffset;
    RawPacket() : kernelTimestampNs(0), block(nullptr), linkLayerOffset(14) {}
};

using MacAddress = std::array<uint8_t, 6>;

struct ParsedPacket {
    uint64_t id = 0;
    int64_t timestamp = 0;

    uint32_t srcIp = 0;
    uint32_t dstIp = 0;

    MacAddress srcMac = {0};
    MacAddress dstMac = {0};

    uint16_t srcPort = 0;
    uint16_t dstPort = 0;

    char protocol[8] = {0};

    uint32_t length = 0;
    uint32_t totalLen = 0;

    std::string tcpFlags;
    uint8_t ttl = 0;

    std::vector<uint8_t> payloadData;
    size_t payloadSize = 0;

    BlockPtr block;

    // 辅助：惰性转换函数 (Lazy Converters)
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

    QString getSummary() const {
        if (srcPort == 0 && dstPort == 0) {
            return QString("%1 %2 -> %3 Len=%4")
                   .arg(protocol, getSrcIpStr(), getDstIpStr(), QString::number(length));
        }
        return QString("%1 %2 -> %3 %4 Len=%5")
               .arg(protocol, QString::number(srcPort), QString::number(dstPort),
                    QString::fromStdString(tcpFlags), QString::number(length));
    }
};

// 告警信息
struct Alert {
    // 将枚举放入结构体内部
    enum Level { Info, Low, Medium, High, Critical };

    uint64_t timestamp;
    Level level;
    uint32_t sourceIp;
    std::string description;
    std::string ruleName;
};

// 注册元类型，允许在信号槽中传递 Alert
Q_DECLARE_METATYPE(Alert)
Q_DECLARE_METATYPE(ParsedPacket)