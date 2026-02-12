#pragma once
#include <QAbstractTableModel>
#include <vector>
#include "common/types/NetworkTypes.h"

class TrafficTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TrafficTableModel(QObject *parent = nullptr);

    // 必须实现的接口
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 添加新数据
    void addPacket(const ParsedPacket& packet);

private:
    std::vector<ParsedPacket> m_packetList;
    const int m_maxRows = 1000; // 限制最大行数，防止内存爆炸
};