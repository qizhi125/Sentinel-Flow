#pragma once
#include <QAbstractTableModel>
#include <deque>
#include <vector>
#include "common/types/NetworkTypes.h"

class TrafficTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TrafficTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addPackets(const std::vector<ParsedPacket>& packets);
    void updateLastPacket(const ParsedPacket& packet, int repeatCount);
    void clear();

    const ParsedPacket* getPacketAt(int row) const;

private:
    std::deque<ParsedPacket> m_packetList;
    std::deque<int> m_repeatCounts;

    const size_t m_maxRows = 100000;
};