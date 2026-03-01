#include "PacketDetailRenderer.h"
#include <QDateTime>

QString PacketDetailRenderer::generateWiresharkHexDump(const std::vector<uint8_t>& data) {
    QString result;
    const int bytesPerLine = 16;
    for (size_t i = 0; i < data.size(); i += bytesPerLine) {
        result += QString("%1  ").arg(i, 4, 16, QLatin1Char('0')).toUpper();
        QString hexPart;
        QString asciiPart;
        for (size_t j = 0; j < bytesPerLine; ++j) {
            if (i + j < data.size()) {
                uint8_t b = data[i + j];
                hexPart += QString("%1 ").arg(b, 2, 16, QLatin1Char('0')).toUpper();
                asciiPart += (b >= 32 && b <= 126) ? QChar(b) : QChar('.');
            } else {
                hexPart += "   ";
            }
            if (j == 7) hexPart += " ";
        }
        result += hexPart + "  " + asciiPart + "\n";
    }
    return result;
}

QTreeWidgetItem* PacketDetailRenderer::addTreeItem(QTreeWidget* tree, QTreeWidgetItem *parent, const QString &title, const QString &value) {
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, value.isEmpty() ? title : QString("%1: %2").arg(title, value));
    return item;
}

void PacketDetailRenderer::render(const ParsedPacket* pkt, QTextEdit* hexView, QTreeWidget* protoTree, QLabel* summaryLabel) {
    if (!pkt) {
        summaryLabel->setText("选择数据包以查看证据详情");
        hexView->clear();
        protoTree->clear();
        return;
    }

    // 1. 渲染顶部摘要
    QDateTime time = QDateTime::fromMSecsSinceEpoch(pkt->timestamp);
    summaryLabel->setText(QString("📦 #%1 | %2 | %3 -> %4 | %5")
            .arg(pkt->id).arg(time.toString("HH:mm:ss.zzz"))
            .arg(pkt->getSrcStr(), pkt->getDstStr(), pkt->getSummary()));

    // 2. 渲染 Hex 视图
    hexView->setText(generateWiresharkHexDump(pkt->payloadData));
    hexView->setStyleSheet("font-family: 'JetBrains Mono', 'Consolas', monospace; font-size: 13px; line-height: 1.5;");

    // 3. 渲染协议树
    protoTree->setUpdatesEnabled(false);
    protoTree->clear();

    // --- L1/L2 ---
    auto* itemFrame = addTreeItem(protoTree, nullptr, "📦 Frame (物理帧)", QString("%1 bytes on wire").arg(pkt->totalLen + 14));
    addTreeItem(protoTree, itemFrame, "Arrival Time", time.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    auto* itemEth = addTreeItem(protoTree, nullptr, "🔗 [L2] Ethernet II", "Src: MAC (Masked), Dst: MAC (Masked)");
    addTreeItem(protoTree, itemEth, "Type", QString("%1 (0x0800)").arg(strcmp(pkt->protocol, "ARP") == 0 ? "ARP" : "IPv4"));

    // --- L3 ---
    auto* itemIP = addTreeItem(protoTree, nullptr, "🌐 [L3] Internet Protocol Version 4", QString("Src: %1, Dst: %2").arg(pkt->getSrcIpStr(), pkt->getDstIpStr()));
    addTreeItem(protoTree, itemIP, "Total Length", QString::number(pkt->totalLen));
    addTreeItem(protoTree, itemIP, "Protocol", QString(pkt->protocol));
    auto* itemSrcIp = addTreeItem(protoTree, itemIP, "Source Address", pkt->getSrcIpStr());
    itemSrcIp->setForeground(0, QColor("#569CD6"));
    auto* itemDstIp = addTreeItem(protoTree, itemIP, "Destination Address", pkt->getDstIpStr());
    itemDstIp->setForeground(0, QColor("#569CD6"));

    // --- L4 ---
    if (strcmp(pkt->protocol, "TCP") == 0) {
        auto* itemTcp = addTreeItem(protoTree, nullptr, "⚙️ [L4] Transmission Control Protocol", QString("Src Port: %1, Dst Port: %2").arg(pkt->srcPort).arg(pkt->dstPort));
        addTreeItem(protoTree, itemTcp, "Source Port", QString::number(pkt->srcPort));
        addTreeItem(protoTree, itemTcp, "Destination Port", QString::number(pkt->dstPort));
        QString flagsStr = QString::fromStdString(pkt->tcpFlags);
        auto* itemFlags = addTreeItem(protoTree, itemTcp, "Flags", QString("[%1]").arg(flagsStr.isEmpty() ? "NONE" : flagsStr));
        itemFlags->setForeground(0, QColor("#CE9178"));
        addTreeItem(protoTree, itemFlags, "URG", flagsStr.contains("URG") ? "1 (Set)" : "0 (Not set)");
        addTreeItem(protoTree, itemFlags, "ACK", flagsStr.contains("ACK") ? "1 (Set)" : "0 (Not set)");
        addTreeItem(protoTree, itemFlags, "PSH", flagsStr.contains("PSH") ? "1 (Set)" : "0 (Not set)");
        addTreeItem(protoTree, itemFlags, "RST", flagsStr.contains("RST") ? "1 (Set)" : "0 (Not set)");
        addTreeItem(protoTree, itemFlags, "SYN", flagsStr.contains("SYN") ? "1 (Set)" : "0 (Not set)");
        addTreeItem(protoTree, itemFlags, "FIN", flagsStr.contains("FIN") ? "1 (Set)" : "0 (Not set)");
    } else if (strcmp(pkt->protocol, "UDP") == 0) {
        auto* itemUdp = addTreeItem(protoTree, nullptr, "⚙️ [L4] User Datagram Protocol", QString("Src Port: %1, Dst Port: %2").arg(pkt->srcPort).arg(pkt->dstPort));
        addTreeItem(protoTree, itemUdp, "Source Port", QString::number(pkt->srcPort));
        addTreeItem(protoTree, itemUdp, "Destination Port", QString::number(pkt->dstPort));
        addTreeItem(protoTree, itemUdp, "Length", QString("%1 bytes").arg(pkt->payloadSize + 8));
    }

    // --- L7 ---
    if (pkt->payloadSize > 0) {
        auto* itemPayload = addTreeItem(protoTree, nullptr, "📄 [L7] Application Data", QString("%1 bytes").arg(pkt->payloadSize));
        QString rawString;
        int printableChars = 0;
        for (uint8_t b : pkt->payloadData) {
            if (b >= 32 && b <= 126) printableChars++;
            rawString += ((b >= 32 && b <= 126) || b == '\n' || b == '\r') ? QChar(b) : QChar('.');
        }

        if (printableChars > pkt->payloadSize * 0.35) {
            QStringList lines = rawString.split('\n');
            int linesToShow = std::min(15, (int)lines.size());
            for (int i = 0; i < linesToShow; ++i) {
                QString line = lines[i].trimmed();
                if (!line.isEmpty()) {
                    auto* node = addTreeItem(protoTree, itemPayload, "Data", line);
                    node->setForeground(0, QColor("#E6DB74"));
                }
            }
            if (lines.size() > 15) addTreeItem(protoTree, itemPayload, "...", QString("(Truncated %1 lines)").arg(lines.size() - 15));
        } else {
             QString hexPrev;
             size_t prevLen = std::min((size_t)32, pkt->payloadData.size());
             for (size_t i = 0; i < prevLen; ++i) hexPrev += QString("%1 ").arg(pkt->payloadData[i], 2, 16, QLatin1Char('0')).toUpper();
             auto* node = addTreeItem(protoTree, itemPayload, "Binary Prefix", hexPrev + "...");
             node->setForeground(0, QColor("#A6E22E"));
        }
    } else {
        addTreeItem(protoTree, nullptr, "📄 [L7] Application Data", "0 bytes (无有效载荷)");
    }

    protoTree->expandAll();
    protoTree->setUpdatesEnabled(true);
}