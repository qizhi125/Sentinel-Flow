#include "presentation/views/pages/TrafficMonitorPage.h"
#include "presentation/views/styles/TrafficMonitorStyle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QLabel>
#include <QDateTime>
#include <iomanip>
#include <sstream>
#include <QScrollBar>
#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFileDialog>
#include <QHostAddress>

TrafficMonitorPage::TrafficMonitorPage(QWidget *parent) : QWidget(parent) {
    setupUi();
    uiTimer = new QTimer(this);
    uiTimer->setInterval(200);
    connect(uiTimer, &QTimer::timeout, this, &TrafficMonitorPage::processPendingPackets);
    uiTimer->start();
}

void TrafficMonitorPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Toolbar
    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(12);

    searchBox = new QLineEdit();
    searchBox->setPlaceholderText("🔍 过滤: 协议 / 端口 / IP");
    searchBox->setFixedWidth(300);
    searchBox->setStyleSheet(TrafficStyle::getSearchBox());
    // 🔥 优化：连接搜索框信号，实现实时搜索
    connect(searchBox, &QLineEdit::textChanged, this, &TrafficMonitorPage::onFilterChanged);

    comboProtocol = new QComboBox();
    comboProtocol->addItems({"全部协议", "TCP", "UDP", "HTTP", "TLS", "ICMP"});
    comboProtocol->setFixedWidth(120);
    comboProtocol->setStyleSheet(TrafficStyle::getComboBox());
    // 连接协议下拉框信号
    connect(comboProtocol, &QComboBox::currentIndexChanged, this, &TrafficMonitorPage::onFilterChanged);

    chkAutoScroll = new QCheckBox("自动滚动");
    chkAutoScroll->setChecked(true);
    chkAutoScroll->setStyleSheet(TrafficStyle::getCheckBox());

    chkCollapse = new QCheckBox("合并重复行");
    chkCollapse->setChecked(false);
    chkCollapse->setStyleSheet(TrafficStyle::getCheckBox());

    btnPause = new QPushButton("⏸ 暂停");
    btnPause->setCheckable(true);
    btnPause->setStyleSheet(TrafficStyle::getBtnNormal());
    connect(btnPause, &QPushButton::toggled, [this](bool checked){
        isPaused = checked;
        btnPause->setText(checked ? "▶ 继续" : "⏸ 暂停");
        btnPause->setStyleSheet(checked ? TrafficStyle::getBtnPaused() : TrafficStyle::getBtnNormal());
    });

    btnExport = new QPushButton("📥 导出");
    btnExport->setStyleSheet(TrafficStyle::getBtnNormal());
    connect(btnExport, &QPushButton::clicked, this, &TrafficMonitorPage::onExportClicked);

    btnClear = new QPushButton("🗑 清空");
    btnClear->setStyleSheet(TrafficStyle::getBtnDanger());

    connect(btnClear, &QPushButton::clicked, [this](){
        table->setRowCount(0);
        packetBuffer.clear();

        // 使用 vector 的 clear 方法
        pendingPackets.clear();

        hexView->clear();
        protoTree->clear();
        m_localSeq = 0;
        lblSelectedInfo->setText("选择数据包以查看详情");
    });

    toolbar->addWidget(searchBox);
    toolbar->addWidget(comboProtocol);
    toolbar->addWidget(chkAutoScroll);
    toolbar->addWidget(chkCollapse);
    toolbar->addStretch();
    toolbar->addWidget(btnExport);
    toolbar->addWidget(btnPause);
    toolbar->addWidget(btnClear);
    mainLayout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setHandleWidth(1);

    table = new QTableWidget();
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"序号", "时间", "源地址", "目的地址", "协议", "长度", "信息"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setShowGrid(false);

    table->verticalHeader()->setDefaultSectionSize(32);

    connect(table, &QTableWidget::itemSelectionChanged, [this](){
        int row = table->currentRow();
        if (row >= 0) updateHexView(row);
    });
    splitter->addWidget(table);

    auto *detailContainer = new QWidget();
    detailContainer->setObjectName("Card");
    auto *vDetailLayout = new QVBoxLayout(detailContainer);

    lblSelectedInfo = new QLabel("选择数据包以查看详情");
    lblSelectedInfo->setStyleSheet(TrafficStyle::SelectedInfo);
    vDetailLayout->addWidget(lblSelectedInfo);

    auto *hDetailLayout = new QHBoxLayout();

    protoTree = new QTreeWidget();
    protoTree->setHeaderLabel("协议详情");
    protoTree->setStyleSheet(TrafficStyle::ProtoTree);
    hDetailLayout->addWidget(protoTree, 4);

    hexView = new QTextEdit();
    hexView->setReadOnly(true);
    hexView->setStyleSheet(TrafficStyle::getHexViewStyle());
    hDetailLayout->addWidget(hexView, 6);

    vDetailLayout->addLayout(hDetailLayout);
    splitter->addWidget(detailContainer);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);

    mainLayout->addWidget(splitter);
}

QTreeWidgetItem* TrafficMonitorPage::addTreeItem(QTreeWidgetItem *parent, const QString &title, const QString &value) {
    QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(protoTree);
    item->setText(0, value.isEmpty() ? title : QString("%1: %2").arg(title, value));
    return item;
}

void TrafficMonitorPage::addPacket(const ParsedPacket& packet) {
    if (isPaused) return;
    QString filter = searchBox->text();
    if (!filter.isEmpty()) {
        QString content = packet.getSrcStr() + packet.getDstStr() + QString(packet.protocol) + packet.getSummary();
        if (!content.contains(filter, Qt::CaseInsensitive)) return;
    }
    if (pendingPackets.size() < 5000) {
        pendingPackets.push_back(packet);
    }
}

void TrafficMonitorPage::processPendingPackets() {
    if (pendingPackets.empty()) return;
    if (!table || !chkAutoScroll || !chkCollapse) return;

    const int BATCH_PROCESS_LIMIT = 200;

    QString filterText = searchBox->text();
    QString filterProto = comboProtocol->currentText();
    bool hasProtoFilter = (filterProto != "全部协议");

    table->setUpdatesEnabled(false);
    table->setSortingEnabled(false);

    int processedCount = 0;

    while (processedCount < BATCH_PROCESS_LIMIT && !pendingPackets.empty()) {
        // 1. 取出头部数据
        ParsedPacket packet = pendingPackets.front();
        pendingPackets.pop_front(); // O(1) 操作，性能极大提升
        processedCount++;

        bool match = true;
        if (hasProtoFilter) {
            if (QString(packet.protocol) != filterProto) match = false;
        }

        if (match && !filterText.isEmpty()) {
            QString content = packet.getSrcStr() + packet.getDstStr() + QString(packet.protocol) + packet.getSummary();
            if (!content.contains(filterText, Qt::CaseInsensitive)) match = false;
        }

        if (!match) continue;

        bool merged = false;
        if (chkCollapse->isChecked() && !packetBuffer.empty()) {
            ParsedPacket& last = packetBuffer.back();
            if (last.srcIp == packet.srcIp && last.dstIp == packet.dstIp && strcmp(last.protocol, packet.protocol) == 0) {
                merged = true;
                lastRowRepeatCount++;
                last = packet;
                int lastRow = table->rowCount() - 1;
                if (lastRow >= 0) {
                    QDateTime time = QDateTime::fromMSecsSinceEpoch(packet.timestamp);
                    table->item(lastRow, 1)->setText(time.toString("HH:mm:ss.zzz"));
                    table->item(lastRow, 6)->setText(packet.getSummary() + QString(" [x%1]").arg(lastRowRepeatCount));
                }
            }
        }

        if (!merged) {
            lastRowRepeatCount = 1;
            if (packetBuffer.size() >= MAX_BUFFER_SIZE) {
                packetBuffer.erase(packetBuffer.begin());
                if (table->rowCount() > 0) table->removeRow(0);
            }
            packetBuffer.push_back(packet);

            int row = table->rowCount();
            table->insertRow(row);
            m_localSeq++;

            QDateTime time = QDateTime::fromMSecsSinceEpoch(packet.timestamp);

            table->setItem(row, 0, new QTableWidgetItem(QString::number(m_localSeq)));
            table->setItem(row, 1, new QTableWidgetItem(time.toString("HH:mm:ss.zzz")));
            table->setItem(row, 2, new QTableWidgetItem(packet.getSrcStr()));
            table->setItem(row, 3, new QTableWidgetItem(packet.getDstStr()));

            QString protoStr = QString(packet.protocol);
            auto *protoItem = new QTableWidgetItem(protoStr);
            if (protoStr == "TCP") protoItem->setForeground(QColor(TrafficStyle::ColorTCP));
            else if (protoStr == "UDP") protoItem->setForeground(QColor(TrafficStyle::ColorUDP));
            else if (protoStr == "HTTP") protoItem->setForeground(QColor(TrafficStyle::ColorHTTP));
            else if (protoStr == "TLS") protoItem->setForeground(QColor(TrafficStyle::ColorTLS));

            table->setItem(row, 4, protoItem);
            table->setItem(row, 5, new QTableWidgetItem(QString::number(packet.totalLen)));
            table->setItem(row, 6, new QTableWidgetItem(packet.getSummary()));
        }
    }

    if (chkAutoScroll->isChecked()) {
        table->scrollToBottom();
    }
    table->setUpdatesEnabled(true);
}

void TrafficMonitorPage::onExportClicked() {
    if (packetBuffer.empty()) {
        QMessageBox::information(this, "导出", "没有数据可导出！");
        return;
    }
    QString fileName = QFileDialog::getSaveFileName(this, "导出数据", "", "CSV 文件 (*.csv);;JSON 文件 (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    if (fileName.endsWith(".json", Qt::CaseInsensitive)) {
        QJsonArray jsonArray;
        for (const auto& pkt : packetBuffer) {
            QJsonObject obj;
            obj["no"] = static_cast<qint64>(pkt.id);
            QDateTime time = QDateTime::fromMSecsSinceEpoch(pkt.timestamp);
            obj["time"] = time.toString("HH:mm:ss.zzz");
            obj["src"] = pkt.getSrcStr();
            obj["dst"] = pkt.getDstStr();
            obj["proto"] = QString(pkt.protocol);
            obj["len"] = static_cast<qint64>(pkt.totalLen);
            obj["info"] = pkt.getSummary();
            jsonArray.append(obj);
        }
        out << QJsonDocument(jsonArray).toJson();
    } else {
        out << "No.,Time,Source,Destination,Protocol,Length,Info\n";
        for (const auto& pkt : packetBuffer) {
            QDateTime time = QDateTime::fromMSecsSinceEpoch(pkt.timestamp);
            out << m_localSeq << "," << time.toString("HH:mm:ss.zzz") << ","
                << pkt.getSrcStr() << "," << pkt.getDstStr() << ","
                << QString(pkt.protocol) << "," << pkt.totalLen << ",\""
                << pkt.getSummary() << "\"\n";
        }
    }
    file.close();
    QMessageBox::information(this, "成功", "数据导出成功！");
}

void TrafficMonitorPage::updateHexView(int row) {
    if (row < 0 || row >= (int)packetBuffer.size()) return;
    const auto& packet = packetBuffer[row];

    QDateTime time = QDateTime::fromMSecsSinceEpoch(packet.timestamp);
    QString headerInfo = QString("📦 #%1 | %2 | %3 | %4 -> %5 | %6")
            .arg(table->item(row, 0) ? table->item(row, 0)->text() : "0")
            .arg(time.toString("HH:mm:ss.zzz"))
            .arg(QString(packet.protocol))
            .arg(packet.getSrcStr())
            .arg(packet.getDstStr())
            .arg(packet.getSummary());
    lblSelectedInfo->setText(headerInfo);

    const std::vector<uint8_t>& data = packet.payloadData;
    if (data.empty()) {
        hexView->setText("无载荷内容 (仅报头)");
    } else {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < data.size(); i += 16) {
            ss << std::setw(4) << i << "  ";
            for (size_t j = 0; j < 16; ++j) {
                if (i + j < data.size()) ss << std::setw(2) << static_cast<int>(data[i + j]) << " ";
                else ss << "   ";
                if (j == 7) ss << " ";
            }
            ss << "  ";
            for (size_t j = 0; j < 16; ++j) {
                if (i + j < data.size()) {
                    unsigned char c = data[i + j];
                    ss << (c >= 32 && c <= 126 ? (char)c : '.');
                }
            }
            ss << "\n";
        }
        hexView->setText(QString::fromStdString(ss.str()).toUpper());
    }

    protoTree->clear();
    QTreeWidgetItem *itemFrame = addTreeItem(nullptr, QString("Frame: %1 bytes").arg(packet.totalLen));
    addTreeItem(itemFrame, "Arrival Time", time.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    QTreeWidgetItem *itemIP = addTreeItem(nullptr, "Internet Protocol Version 4");
    addTreeItem(itemIP, "Source", packet.getSrcIpStr());
    addTreeItem(itemIP, "Destination", packet.getDstIpStr());
}

void TrafficMonitorPage::refreshTable() {
    table->setUpdatesEnabled(false);
    table->setRowCount(0);
    m_localSeq = 0;

    QString filterText = searchBox->text();
    QString filterProto = comboProtocol->currentText();
    bool hasProtoFilter = (filterProto != "全部协议");

    for (const auto& packet : packetBuffer) {
        bool match = true;
        if (hasProtoFilter && QString(packet.protocol) != filterProto) match = false;

        if (match && !filterText.isEmpty()) {
            QString content = packet.getSrcStr() + packet.getDstStr() + QString(packet.protocol) + packet.getSummary();
            if (!content.contains(filterText, Qt::CaseInsensitive)) match = false;
        }

        if (match) {
            int row = table->rowCount();
            table->insertRow(row);
            m_localSeq++;

            QDateTime time = QDateTime::fromMSecsSinceEpoch(packet.timestamp);

            table->setItem(row, 0, new QTableWidgetItem(QString::number(m_localSeq)));
            table->setItem(row, 1, new QTableWidgetItem(time.toString("HH:mm:ss.zzz")));
            table->setItem(row, 2, new QTableWidgetItem(packet.getSrcStr()));
            table->setItem(row, 3, new QTableWidgetItem(packet.getDstStr()));

            QString protoStr = QString(packet.protocol);
            auto *protoItem = new QTableWidgetItem(protoStr);
            if (protoStr == "TCP") protoItem->setForeground(QColor(TrafficStyle::ColorTCP));
            else if (protoStr == "UDP") protoItem->setForeground(QColor(TrafficStyle::ColorUDP));
            else if (protoStr == "HTTP") protoItem->setForeground(QColor(TrafficStyle::ColorHTTP));
            else if (protoStr == "TLS") protoItem->setForeground(QColor(TrafficStyle::ColorTLS));

            table->setItem(row, 4, protoItem);
            table->setItem(row, 5, new QTableWidgetItem(QString::number(packet.totalLen)));
            table->setItem(row, 6, new QTableWidgetItem(packet.getSummary()));
        }
    }
    if (chkAutoScroll->isChecked()) table->scrollToBottom();
    table->setUpdatesEnabled(true);
}

void TrafficMonitorPage::onFilterChanged() {
    refreshTable();
}