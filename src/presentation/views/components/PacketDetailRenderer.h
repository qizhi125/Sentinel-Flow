#pragma once
#include "common/types/NetworkTypes.h"
#include <QTextEdit>
#include <QTreeWidget>
#include <QLabel>
#include <QString>

class PacketDetailRenderer {
public:
    static void render(const ParsedPacket* pkt, QTextEdit* hexView, QTreeWidget* protoTree, QLabel* summaryLabel);

private:
    static QString generateWiresharkHexDump(const std::vector<uint8_t>& data);
    static QTreeWidgetItem* addTreeItem(QTreeWidget* tree, QTreeWidgetItem *parent, const QString &title, const QString &value = "");
};