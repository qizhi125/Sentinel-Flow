#include "presentation/views/components/TrafficTableModel.h"
#include <QColor>

TrafficTableModel::TrafficTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int TrafficTableModel::rowCount(const QModelIndex &) const {
    return static_cast<int>(m_packetList.size());
}

int TrafficTableModel::columnCount(const QModelIndex &) const {
    return 6; // Time, Protocol, Source, Dest, Port, Size
}

QVariant TrafficTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
        case 0: return "Protocol";
        case 1: return "Source IP";
        case 2: return "Dest IP";
        case 3: return "Port";
        case 4: return "Size";
        case 5: return "Info";
        default: return QVariant();
    }
}

QVariant TrafficTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_packetList.size()))
        return QVariant();

    const auto& pkt = m_packetList[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return pkt.protocol;
            case 1: return pkt.getSrcIpStr();
            case 2: return pkt.getDstIpStr();
            case 3: return pkt.dstPort;
            case 4: return QString::number(pkt.payloadSize) + " B";
            case 5: return pkt.getSummary();
        }
    } else if (role == Qt::ForegroundRole) {
        QString proto = QString(pkt.protocol);

        if (proto == "TCP") return QColor("#2ecc71");
        if (proto == "UDP") return QColor("#3498db");
        return QColor("#95a5a6");
    }

    return QVariant();
}

void TrafficTableModel::addPacket(const ParsedPacket& packet) {
    // 限制行数：如果超过，删除最旧的
    if (m_packetList.size() >= static_cast<size_t>(m_maxRows)) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_packetList.erase(m_packetList.begin());
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), m_packetList.size(), m_packetList.size());
    m_packetList.push_back(packet);
    endInsertRows();
}