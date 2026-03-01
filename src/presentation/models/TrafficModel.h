#pragma once
#include <QObject>
#include <QMutex>
#include <deque>
#include "common/types/NetworkTypes.h"

class TrafficModel : public QObject {
    Q_OBJECT
public:
    static TrafficModel& instance();

    void addPacket(const ParsedPacket& packet);
    std::deque<ParsedPacket> getRecentPackets(int max = 100) const;

    uint64_t totalBytes() const;
    uint64_t totalPackets() const;
    QMap<QString, uint64_t> protocolBytes() const;

    signals:
        void dataUpdated();

private:
    TrafficModel() = default;
    mutable QMutex mutex_;
    std::deque<ParsedPacket> packets_;
    uint64_t totalBytes_ = 0;
    uint64_t totalPackets_ = 0;
    QMap<QString, uint64_t> protocolBytes_;
};