#include "presentation/views/components/PacketDetailRenderer.h"
#include "presentation/views/pages/TrafficMonitorPage.h"
#include "presentation/views/styles/TrafficMonitorStyle.h"
#include "common/utils/StringUtils.h"
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QLabel>
#include <QDateTime>
#include <QScrollBar>
#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileDialog>
#include <QHostAddress>
#include <QElapsedTimer>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QSaveFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>

class TrafficProxyModel : public QSortFilterProxyModel {
public:
    explicit TrafficProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    QString filterText;
    QString filterProto = "全部协议";

    void setFilter(const QString& text, const QString& proto) {
        beginFilterChange();
        filterText = text;
        filterProto = proto;
        endFilterChange();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        Q_UNUSED(source_parent);

        auto* model = qobject_cast<TrafficTableModel*>(sourceModel());
        if (!model) return false;

        const ParsedPacket* pkt = model->getPacketAt(source_row);
        if (!pkt) return false;

        if (filterProto != "全部协议" && QString(pkt->protocol) != filterProto) {
            return false;
        }

        if (!filterText.isEmpty()) {
            if (QString(pkt->protocol).contains(filterText, Qt::CaseInsensitive)) return true;
            if (pkt->getSrcStr().contains(filterText, Qt::CaseInsensitive)) return true;
            if (pkt->getDstStr().contains(filterText, Qt::CaseInsensitive)) return true;
            if (pkt->getSummary().contains(filterText, Qt::CaseInsensitive)) return true;

            return false;
        }
        return true;
    }
};

TrafficMonitorPage::TrafficMonitorPage(QWidget *parent) : QWidget(parent) {
    setupUi();

    uiTimer = new QTimer(this);
    uiTimer->setInterval(200);
    connect(uiTimer, &QTimer::timeout, this, &TrafficMonitorPage::processPendingPackets);
    uiTimer->start();

    filterDebounceTimer = new QTimer(this);
    filterDebounceTimer->setSingleShot(true);
    filterDebounceTimer->setInterval(300);
    connect(filterDebounceTimer, &QTimer::timeout, this, &TrafficMonitorPage::refreshTable);

    tableView->installEventFilter(this);
}

void TrafficMonitorPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(12);

    searchBox = new QLineEdit();
    searchBox->setPlaceholderText("🔍 过滤: 协议 / 端口 / IP");
    searchBox->setFixedWidth(300);
    searchBox->setStyleSheet(TrafficStyle::getSearchBox());

    connect(searchBox, &QLineEdit::textChanged, this, &TrafficMonitorPage::onFilterChanged);

    comboProtocol = new QComboBox();
    comboProtocol->addItems({"全部协议", "TCP", "UDP", "HTTP", "TLS", "ICMP"});
    comboProtocol->setFixedWidth(120);
    comboProtocol->setStyleSheet(TrafficStyle::getComboBox());

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
        tableModel->clear();
        pendingPackets.clear();
        hexView->clear();
        protoTree->clear();
        lastRowRepeatCount = 1;
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

    tableView = new QTableView();
    tableModel = new TrafficTableModel(this);
    proxyModel = new TrafficProxyModel(this);
    proxyModel->setSourceModel(tableModel);
    tableView->setModel(proxyModel);

    tableView->setShowGrid(false);
    tableView->verticalHeader()->setVisible(false);
    tableView->verticalHeader()->setDefaultSectionSize(32);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    tableView->setColumnWidth(0, 70);
    tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    tableView->setColumnWidth(1, 110);

    tableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableView->setColumnWidth(2, 180);
    tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    tableView->setColumnWidth(3, 180);

    tableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    tableView->setColumnWidth(4, 80);
    tableView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    tableView->setColumnWidth(5, 80);

    tableView->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    connect(tableView->selectionModel(), &QItemSelectionModel::currentChanged, [this](const QModelIndex &current, const QModelIndex &){
        if (current.isValid()) {
            updateHexView(current.row());
        }
    });
    splitter->addWidget(tableView);

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

    hexView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(hexView, &QTextEdit::customContextMenuRequested, this, &TrafficMonitorPage::onHexViewContextMenu);

    hDetailLayout->addWidget(hexView, 6);

    vDetailLayout->addLayout(hDetailLayout);
    splitter->addWidget(detailContainer);
    splitter->setStretchFactor(0, 7);
    splitter->setStretchFactor(1, 3);

    mainLayout->addWidget(splitter);
}

void TrafficMonitorPage::onHexViewContextMenu(const QPoint& pos) {
    QModelIndexList selected = tableView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;

    QModelIndex sourceIdx = proxyModel->mapToSource(selected.first());
    const ParsedPacket* packet = tableModel->getPacketAt(sourceIdx.row());

    if (!packet || packet->payloadData.empty()) {
        return;
    }

    QMenu contextMenu(this);
    QAction *actHexStream = contextMenu.addAction("📄 复制为纯十六进制流 (Hex Stream)");
    QAction *actHexDump   = contextMenu.addAction("📄 复制为当前视图 (Hex Dump)");
    QAction *actPrintable = contextMenu.addAction("📄 复制为可打印字符 (Printable Text)");

    QAction *selectedAction = contextMenu.exec(hexView->mapToGlobal(pos));
    if (!selectedAction) return;

    QString clipboardText;

    if (selectedAction == actHexStream) {
        clipboardText.reserve(packet->payloadData.size() * 2);
        static const char hexChars[] = "0123456789ABCDEF";
        for (uint8_t byte : packet->payloadData) {
            clipboardText.append(hexChars[byte >> 4]);
            clipboardText.append(hexChars[byte & 0xF]);
        }
    } else if (selectedAction == actHexDump) {
        clipboardText = hexView->toPlainText();
    } else if (selectedAction == actPrintable) {
        clipboardText.reserve(packet->payloadData.size());
        for (uint8_t byte : packet->payloadData) {
            clipboardText.append((byte >= 32 && byte <= 126) ? QChar(byte) : QChar('.'));
        }
    }

    if (!clipboardText.isEmpty()) {
        QApplication::clipboard()->setText(clipboardText);
    }
}

void TrafficMonitorPage::addPacket(const ParsedPacket& packet) {
    if (isPaused) return;

    if (pendingPackets.size() < 100000)
        pendingPackets.push_back(packet);
}

void TrafficMonitorPage::processPendingPackets() {
    if (hoverPaused || pendingPackets.empty()) return;
    if (!tableView || !chkAutoScroll || !chkCollapse) return;

    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    timer.restart();

    std::vector<ParsedPacket> batchToAdd;

    while (!pendingPackets.empty() && timer.elapsed() < 5) {
        ParsedPacket pkt = pendingPackets.front();
        pendingPackets.pop_front();

        bool merged = false;
        if (chkCollapse->isChecked()) {
            const ParsedPacket* lastPkt = nullptr;
            if (!batchToAdd.empty()) {
                lastPkt = &batchToAdd.back();
            } else if (tableModel->rowCount() > 0) {
                lastPkt = tableModel->getPacketAt(tableModel->rowCount() - 1);
            }

            if (lastPkt && lastPkt->srcIp == pkt.srcIp && lastPkt->dstIp == pkt.dstIp &&
                std::strcmp(lastPkt->protocol, pkt.protocol) == 0) {

                merged = true;
                lastRowRepeatCount++;

                if (!batchToAdd.empty()) {
                    batchToAdd.back() = pkt;
                } else {
                    tableModel->updateLastPacket(pkt, lastRowRepeatCount);
                }
            }
        }

        if (!merged) {
            lastRowRepeatCount = 1;
            batchToAdd.push_back(pkt);
        }
    }

    if (!batchToAdd.empty()) {
        tableModel->addPackets(batchToAdd);
    }

    if (chkAutoScroll->isChecked() && !hoverPaused) {
        tableView->scrollToBottom();
    }
}

void TrafficMonitorPage::onExportClicked() {
    int totalRows = tableModel->rowCount();
    if (totalRows == 0) {
        QMessageBox::information(this, "导出", "当前没有流量数据可导出！");
        return;
    }

    QString selectedFilter;
    QString initialPath = QDir::homePath() + "/Traffic_Export_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
    QString fileName = QFileDialog::getSaveFileName(this, "导出流量数据", initialPath,
                                                     "CSV 文件 (*.csv);;JSON 文件 (*.json)",
                                                     &selectedFilter, QFileDialog::DontUseNativeDialog);
    if (fileName.isEmpty()) return;

    QFileInfo fi(fileName);
    QString targetExt = selectedFilter.contains("json", Qt::CaseInsensitive) ? "json" : "csv";
    if (fi.suffix().toLower() != targetExt) {
        fileName = fi.absolutePath() + "/" + fi.completeBaseName() + "." + targetExt;
    }

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法创建文件，请确认权限！");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    if (targetExt == "json") {
        QJsonArray rootArray;
        for (int i = 0; i < totalRows; ++i) {
            const ParsedPacket* pkt = tableModel->getPacketAt(i);
            if (!pkt) continue;

            QJsonObject obj;
            obj["no"] = static_cast<qint64>(i + 1);
            obj["time"] = QDateTime::fromMSecsSinceEpoch(pkt->timestamp).toString("HH:mm:ss.zzz");
            obj["src"] = pkt->getSrcStr();
            obj["dst"] = pkt->getDstStr();
            obj["proto"] = QString(pkt->protocol);
            obj["len"] = static_cast<qint64>(pkt->totalLen);
            obj["info"] = pkt->getSummary();
            rootArray.append(obj);

            if (i % 500 == 0) QCoreApplication::processEvents();
        }
        out << QJsonDocument(rootArray).toJson(QJsonDocument::Indented);
    } else {
        out << "No.,Time,Source,Destination,Protocol,Length,Info\n";
        for (int i = 0; i < totalRows; ++i) {
            const ParsedPacket* pkt = tableModel->getPacketAt(i);
            if (!pkt) continue;

            QString timeStr = QDateTime::fromMSecsSinceEpoch(pkt->timestamp).toString("HH:mm:ss.zzz");
            QString escapedInfo = pkt->getSummary().replace("\"", "\"\"");

            out << (i + 1) << ","
                << timeStr << ","
                << pkt->getSrcStr() << ","
                << pkt->getDstStr() << ","
                << QString(pkt->protocol) << ","
                << pkt->totalLen << ","
                << "\"" << escapedInfo << "\"\n";

            if (i % 500 == 0) QCoreApplication::processEvents();
        }
    }

    if (file.commit()) {
        QMessageBox::information(this, "成功", QString("已成功导出 %1 条流量审计记录。").arg(totalRows));
    } else {
        QMessageBox::critical(this, "失败", "文件提交失败，请检查磁盘空间。");
    }
}

void TrafficMonitorPage::updateHexView(int row) {
    if (row < 0 || row >= proxyModel->rowCount()) return;
    QModelIndex sourceIdx = proxyModel->mapToSource(proxyModel->index(row, 0));
    const ParsedPacket* pkt = tableModel->getPacketAt(sourceIdx.row());

    PacketDetailRenderer::render(pkt, hexView, protoTree, lblSelectedInfo);
}

void TrafficMonitorPage::refreshTable() {
    proxyModel->setFilter(searchBox->text(), comboProtocol->currentText());
}

void TrafficMonitorPage::onFilterChanged() {
    filterDebounceTimer->start();
}

void TrafficMonitorPage::onThemeChanged() {
    searchBox->setStyleSheet(TrafficStyle::getSearchBox());
    comboProtocol->setStyleSheet(TrafficStyle::getComboBox());
    chkAutoScroll->setStyleSheet(TrafficStyle::getCheckBox());
    chkCollapse->setStyleSheet(TrafficStyle::getCheckBox());
    btnPause->setStyleSheet(isPaused ? TrafficStyle::getBtnPaused() : TrafficStyle::getBtnNormal());
    btnExport->setStyleSheet(TrafficStyle::getBtnNormal());
    btnClear->setStyleSheet(TrafficStyle::getBtnDanger());
    hexView->setStyleSheet(TrafficStyle::getHexViewStyle());
    protoTree->setStyleSheet(TrafficStyle::ProtoTree);
}

bool TrafficMonitorPage::eventFilter(QObject *obj, QEvent *event) {
    if (obj == tableView) {
        if (event->type() == QEvent::Enter) {
            hoverPaused = true;
            return true;
        } else if (event->type() == QEvent::Leave) {
            hoverPaused = false;
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}