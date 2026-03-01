#include "presentation/views/pages/AlertsPage.h"
#include "presentation/views/styles/global.h"
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QTimer>
#include <QDebug>

AlertsPage::AlertsPage(QWidget *parent) : QWidget(parent) {
    setupUi();

    uiRefreshTimer = new QTimer(this);
    connect(uiRefreshTimer, &QTimer::timeout, this, [this]() {
        if (isDirty) {
            refreshTable();
            isDirty = false;
        }
    });
    uiRefreshTimer->start(500);
}

void AlertsPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(12);

    searchBox = new QLineEdit();
    searchBox->setPlaceholderText("🔍 搜索: 规则名 / IP / 描述");
    searchBox->setFixedWidth(300);
    connect(searchBox, &QLineEdit::textChanged, this, &AlertsPage::onFilterChanged);

    levelFilter = new QComboBox();
    levelFilter->addItems({"全部等级", "严重 (Critical)", "高危 (High)", "中危 (Medium)", "低危 (Low)", "信息 (Info)"});
    levelFilter->setFixedWidth(150);
    connect(levelFilter, &QComboBox::currentIndexChanged, this, &AlertsPage::onFilterChanged);

    btnOpenPcapDir = new QPushButton("📂 打开取证目录");
    connect(btnOpenPcapDir, &QPushButton::clicked, this, &AlertsPage::onOpenPcapDirClicked);

    btnExport = new QPushButton("📥 导出报告");
    connect(btnExport, &QPushButton::clicked, this, &AlertsPage::onExportClicked);

    btnClear = new QPushButton("🗑 清空");
    btnClear->setProperty("type", "danger");
    connect(btnClear, &QPushButton::clicked, this, &AlertsPage::onClearClicked);

    toolbar->addWidget(searchBox);
    toolbar->addWidget(levelFilter);
    toolbar->addStretch();
    toolbar->addWidget(btnOpenPcapDir);
    toolbar->addWidget(btnExport);
    toolbar->addWidget(btnClear);
    mainLayout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setHandleWidth(1);

    alertTable = new QTableWidget(0, 5);
    alertTable->setHorizontalHeaderLabels({"时间", "级别", "规则名", "源 IP", "描述"});

    alertTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    alertTable->setSelectionMode(QAbstractItemView::SingleSelection);
    alertTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertTable->verticalHeader()->setVisible(false);
    alertTable->setShowGrid(false);

    alertTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    alertTable->setColumnWidth(0, 150);
    alertTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    alertTable->setColumnWidth(1, 80);
    alertTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    alertTable->setColumnWidth(2, 200);
    alertTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    alertTable->setColumnWidth(3, 120);
    alertTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    connect(alertTable->selectionModel(), &QItemSelectionModel::currentChanged, [this](const QModelIndex &current, const QModelIndex &){
        if (current.isValid() && current.row() < (int)alertsBuffer.size()) {
            updateDetailView(alertsBuffer[current.row()]);
        }
    });

    splitter->addWidget(alertTable);

    auto *detailContainer = new QWidget();
    detailContainer->setObjectName("Card");
    auto *vDetailLayout = new QVBoxLayout(detailContainer);

    lblDetailTitle = new QLabel("告警详情");
    lblDetailTitle->setProperty("role", "title");
    vDetailLayout->addWidget(lblDetailTitle);

    txtDetailContent = new QTextEdit();
    txtDetailContent->setReadOnly(true);
    vDetailLayout->addWidget(txtDetailContent);

    splitter->addWidget(detailContainer);
    splitter->setStretchFactor(0, 6);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);
}

void AlertsPage::addAlert(const Alert& alert) {
    alertsBuffer.push_front(alert);
    if (alertsBuffer.size() > 2000) alertsBuffer.pop_back();
    isDirty = true;
}

void AlertsPage::refreshTable() {
    alertTable->setUpdatesEnabled(false);
    alertTable->setRowCount(0);

    QString filterText = searchBox->text().toLower();
    int filterLevelIdx = levelFilter->currentIndex();

    auto checkLevel = [&](Alert::Level l) {
        if (filterLevelIdx == 0) return true;
        if (filterLevelIdx == 1 && l == Alert::Critical) return true;
        if (filterLevelIdx == 2 && l == Alert::High) return true;
        if (filterLevelIdx == 3 && l == Alert::Medium) return true;
        if (filterLevelIdx == 4 && l == Alert::Low) return true;
        return false;
    };

    for (size_t i = 0; i < alertsBuffer.size(); ++i) {
        const auto& alert = alertsBuffer[i];
        if (!checkLevel(alert.level)) continue;

        if (!filterText.isEmpty()) {
            QString ipStr = QHostAddress(alert.sourceIp).toString();
            QString content = (QString::fromStdString(alert.ruleName) + ipStr + QString::fromStdString(alert.description)).toLower();
            if (!content.contains(filterText)) continue;
        }
        int row = alertTable->rowCount();
        alertTable->insertRow(row);
        QDateTime time = QDateTime::fromSecsSinceEpoch(alert.timestamp);

        auto *itemTime = new QTableWidgetItem(time.toString("HH:mm:ss"));
        itemTime->setData(Qt::UserRole, static_cast<int>(i));

        QString levelStr;
        QColor levelColor;
        switch(alert.level) {
            case Alert::Critical: levelStr="严重"; levelColor = QColor(Style::ColorDanger()); break;
            case Alert::High:     levelStr="高危"; levelColor = QColor(Style::ColorWarning()); break;
            case Alert::Medium:   levelStr="中危"; levelColor = QColor(Style::ColorAccent()); break;
            case Alert::Low:      levelStr="低危"; levelColor = QColor(Style::ColorNeutral()); break;
            default:              levelStr="信息"; levelColor = QColor("#888888"); break;
        }

        auto *itemLevel = new QTableWidgetItem(levelStr);
        itemLevel->setForeground(levelColor);
        itemLevel->setFont(QFont("Inter", 15, QFont::Bold));

        auto *itemRule = new QTableWidgetItem(QString::fromStdString(alert.ruleName));
        auto *itemSrc = new QTableWidgetItem(QHostAddress(alert.sourceIp).toString());

        alertTable->setItem(row, 0, itemTime);
        alertTable->setItem(row, 1, itemLevel);
        alertTable->setItem(row, 2, itemRule);
        alertTable->setItem(row, 3, itemSrc);
    }
    alertTable->setUpdatesEnabled(true);
}

void AlertsPage::onFilterChanged() { refreshTable(); }

void AlertsPage::onClearClicked() {
    alertsBuffer.clear();
    refreshTable();
    txtDetailContent->clear();
    lblDetailTitle->setText("选择告警查看详情");
    btnOpenPcapDir->hide();
}

void AlertsPage::onOpenPcapDirClicked() {
    QString currentPath = QDir::currentPath() + "/evidences";
    QDir().mkpath(currentPath);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath))) {
        QMessageBox::warning(this, "错误", "无法打开本地取证目录: \n" + currentPath);
    }
}

void AlertsPage::updateDetailView(const Alert& alert) {
    lblDetailTitle->setText(QString::fromStdString(alert.ruleName));
    btnOpenPcapDir->setVisible(alert.level >= Alert::High);

    QString t = QDateTime::fromSecsSinceEpoch(alert.timestamp).toString("yyyy-MM-dd HH:mm:ss");
    QString ip = QHostAddress(alert.sourceIp).toString();
    QString d = QString::fromStdString(alert.description).toHtmlEscaped();

    QString textColor = g_isDarkMode ? "#E0E0E0" : "#111111";
    QString boxBg     = g_isDarkMode ? "#111111" : "#F8F9FA";
    QString boxBorder = g_isDarkMode ? "#333333" : "#CCCCCC";
    QString boxText   = g_isDarkMode ? "#E6DB74" : "#111111";

    QString html = QString(R"(
        <div style="font-family: Inter, sans-serif; font-size: 14px; color: %1;">
          <div style="margin-bottom: 15px;">
            <div><span style="color:%2; font-weight:bold;">Time</span>: <b>%3</b></div>
            <div><span style="color:%2; font-weight:bold;">Source</span>: <b>%4</b></div>
          </div>
          <div style="color:%2; font-weight:bold; margin-bottom:8px;">Description</div>
          <pre style="white-space:pre-wrap; margin:0; padding:15px; background:%5; border:1px solid %6; border-radius:6px; color:%7; font-family:'JetBrains Mono'; font-weight:500;">%8</pre>
        </div>
    )").arg(textColor, Style::ColorAccent(), t, ip, boxBg, boxBorder, boxText, d);

    txtDetailContent->setHtml(html);
}

void AlertsPage::onExportClicked() {
    if (alertsBuffer.empty()) {
        QMessageBox::information(this, "导出", "当前没有告警数据！");
        return;
    }

    QString selectedFilter;
    QString initialPath = QDir::homePath() + "/alert_export";
    QString rawFileName = QFileDialog::getSaveFileName(this,
                                                        "导出告警日志",
                                                        initialPath,
                                                        "CSV 文件 (*.csv);;JSON 文件 (*.json)",
                                                        &selectedFilter,
                                                        QFileDialog::DontUseNativeDialog);

    if (rawFileName.isEmpty()) return;

    QString finalFileName = rawFileName;
    QFileInfo fi(rawFileName);

    QString targetExt = selectedFilter.contains("json", Qt::CaseInsensitive) ? "json" : "csv";

    if (fi.suffix().toLower() != targetExt) {
        finalFileName = fi.absolutePath() + "/" + fi.completeBaseName() + "." + targetExt;
    }

    QFile file(finalFileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", QString("无法创建文件，请确认权限：\n%1").arg(finalFileName));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    auto getLevelName = [](Alert::Level level) -> QString {
        switch (level) {
            case Alert::Critical: return "严重";
            case Alert::High:     return "高危";
            case Alert::Medium:   return "中危";
            case Alert::Low:      return "低危";
            default:              return "信息";
        }
    };

    if (finalFileName.endsWith(".json", Qt::CaseInsensitive)) {
        QJsonArray rootArray;
        for (const auto& alert : alertsBuffer) {
            QJsonObject obj;
            obj["timestamp"] = (double)alert.timestamp;
            obj["level"] = getLevelName(alert.level);
            obj["ruleName"] = QString::fromStdString(alert.ruleName);
            obj["sourceIp"] = QHostAddress(alert.sourceIp).toString();
            obj["description"] = QString::fromStdString(alert.description);
            rootArray.append(obj);
        }
        out << QJsonDocument(rootArray).toJson(QJsonDocument::Indented);
    }
    else {
        out << "No.,Time,Level,RuleName,SourceIP,Description\n";
        int count = 1;
        for (const auto& alert : alertsBuffer) {
            QString timeStr = QDateTime::fromSecsSinceEpoch(alert.timestamp).toString("yyyy-MM-dd HH:mm:ss");
            QString desc = QString::fromStdString(alert.description).replace("\"", "\"\"");
            out << count++ << "," << timeStr << "," << getLevelName(alert.level) << ","
                << QString::fromStdString(alert.ruleName) << "," << QHostAddress(alert.sourceIp).toString() << ","
                << "\"" << desc << "\"\n";
        }
    }

    out.flush();
    file.close();

    QMessageBox::information(this, "导出成功", QString("文件已锁定并保存至：\n%1").arg(finalFileName));
}

void AlertsPage::onThemeChanged() {
    if (alertTable->selectedItems().size() > 0) {
        int row = alertTable->currentRow();
        int alertIndex = alertTable->item(row, 0)->data(Qt::UserRole).toInt();
        updateDetailView(alertsBuffer[alertIndex]);
    }
}