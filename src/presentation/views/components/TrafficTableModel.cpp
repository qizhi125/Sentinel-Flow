#include "presentation/views/components/TrafficTableModel.h"
#include "presentation/views/styles/TrafficMonitorStyle.h"
#include <QDateTime>
#include <QColor>

TrafficTableModel::TrafficTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int TrafficTableModel::rowCount(const QModelIndex &) const {
    return static_cast<int>(m_packetList.size());
}

int TrafficTableModel::columnCount(const QModelIndex &) const {
    return 7; // No, Time, Source, Dest, Protocol, Size, Info
}

QVariant TrafficTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
        case 0: return "No.";
        case 1: return "Time";
        case 2: return "Source IP";
        case 3: return "Dest IP";
        case 4: return "Protocol";
        case 5: return "Length";
        case 6: return "Info";
        default: return QVariant();
    }
}

QVariant TrafficTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_packetList.size()))
        return QVariant();

    const auto& pkt = m_packetList[index.row()];
    int repeatCount = m_repeatCounts[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return QString::number(index.row() + 1);
            case 1: return QDateTime::fromMSecsSinceEpoch(pkt.timestamp).toString("HH:mm:ss.zzz");
            case 2: return pkt.getSrcStr();
            case 3: return pkt.getDstStr();
            case 4: return QString(pkt.protocol);
            case 5: return QString::number(pkt.totalLen);
            case 6: {
                QString info = pkt.getSummary();
                if (repeatCount > 1) {
                    info += QString(" [x%1]").arg(repeatCount);
                }
                return info;
            }
        }
    } else if (role == Qt::ForegroundRole) {
        if (index.column() == 4) {
            QString proto = QString(pkt.protocol);
            if (proto == "TCP") return QColor(TrafficStyle::ColorTCP);
            if (proto == "UDP") return QColor(TrafficStyle::ColorUDP);
            if (proto == "HTTP") return QColor(TrafficStyle::ColorHTTP);
            if (proto == "TLS") return QColor(TrafficStyle::ColorTLS);
            return QColor(TrafficStyle::ColorOther);
        }
    } else if (role == Qt::UserRole) {
        if (index.column() == 0) return static_cast<qulonglong>(pkt.id);
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 5) return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    return QVariant();
}

void TrafficTableModel::addPackets(const std::vector<ParsedPacket>& packets) {
    if (packets.empty()) return;

    if (m_packetList.size() + packets.size() > m_maxRows) {
        int removeCount = (m_packetList.size() + packets.size()) - m_maxRows;
        beginRemoveRows(QModelIndex(), 0, removeCount - 1);
        for (int i = 0; i < removeCount; ++i) {
            m_packetList.pop_front();
            m_repeatCounts.pop_front();
        }
        endRemoveRows();
    }

    int firstRow = m_packetList.size();
    beginInsertRows(QModelIndex(), firstRow, firstRow + packets.size() - 1);
    for (const auto& p : packets) {
        m_packetList.push_back(p);
        m_repeatCounts.push_back(1);
    }
    endInsertRows();
}

void TrafficTableModel::updateLastPacket(const ParsedPacket& packet, int repeatCount) {
    if (m_packetList.empty()) return;

    m_packetList.back() = packet;
    m_repeatCounts.back() = repeatCount;

    int lastRow = m_packetList.size() - 1;
    emit dataChanged(index(lastRow, 0), index(lastRow, columnCount() - 1));
}

void TrafficTableModel::clear() {
    beginResetModel();
    m_packetList.clear();
    m_repeatCounts.clear();
    endResetModel();
}

const ParsedPacket* TrafficTableModel::getPacketAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_packetList.size())) return nullptr;
    return &m_packetList[row];
}