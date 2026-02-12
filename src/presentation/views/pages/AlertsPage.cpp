#include "presentation/views/pages/AlertsPage.h"
#include "presentation/views/styles/AlertsStyle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>

AlertsPage::AlertsPage(QWidget *parent) : QWidget(parent) {
    setupUi();
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
    searchBox->setStyleSheet(AlertsStyle::getSearchBox());
    connect(searchBox, &QLineEdit::textChanged, this, &AlertsPage::onFilterChanged);

    levelFilter = new QComboBox();
    levelFilter->addItems({"全部等级", "严重 (Critical)", "高危 (High)", "中危 (Medium)", "低危 (Low)"});
    levelFilter->setStyleSheet(AlertsStyle::getComboBox());
    levelFilter->setFixedWidth(160);
    connect(levelFilter, &QComboBox::currentIndexChanged, this, &AlertsPage::onFilterChanged);

    btnExport = new QPushButton("📥 导出");
    btnExport->setStyleSheet(AlertsStyle::getBtnNormal());
    connect(btnExport, &QPushButton::clicked, this, &AlertsPage::onExportClicked);

    btnClear = new QPushButton("🗑 清空");
    btnClear->setStyleSheet(AlertsStyle::getBtnNormal());
    connect(btnClear, &QPushButton::clicked, this, &AlertsPage::onClearClicked);

    toolbar->addWidget(searchBox);
    toolbar->addWidget(levelFilter);
    toolbar->addStretch();
    toolbar->addWidget(btnExport);
    toolbar->addWidget(btnClear);

    mainLayout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);

    alertTable = new QTableWidget();
    alertTable->setColumnCount(4);
    alertTable->setHorizontalHeaderLabels({"时间", "等级", "规则名称", "源IP"});
    alertTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    alertTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    alertTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    alertTable->setSelectionMode(QAbstractItemView::SingleSelection);
    alertTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alertTable->verticalHeader()->setVisible(false);
    alertTable->setShowGrid(false);
    alertTable->setAlternatingRowColors(true);
    alertTable->verticalHeader()->setDefaultSectionSize(36);

    auto *detailWidget = new QWidget();
    detailWidget->setObjectName("Card");
    auto *detailLayout = new QVBoxLayout(detailWidget);

    lblDetailTitle = new QLabel("选择告警查看详情");
    lblDetailTitle->setStyleSheet(AlertsStyle::DetailTitle);

    txtDetailContent = new QTextEdit();
    txtDetailContent->setReadOnly(true);
    txtDetailContent->setStyleSheet(AlertsStyle::PayloadBox);

    detailLayout->addWidget(lblDetailTitle);
    detailLayout->addWidget(txtDetailContent);

    splitter->addWidget(alertTable);
    splitter->addWidget(detailWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter);

    connect(alertTable, &QTableWidget::itemClicked, [this](QTableWidgetItem *item) {
            int alertIndex = item->data(Qt::UserRole).toInt();
            if (alertIndex >= 0 && alertIndex < (int)alertsBuffer.size()) {
                updateDetailView(alertsBuffer[alertIndex]);
            }
        });
}

void AlertsPage::addAlert(const Alert& alert) {
    alertsBuffer.insert(alertsBuffer.begin(), alert);
    if (alertsBuffer.size() > 2000) alertsBuffer.pop_back();

    bool isFiltersEmpty = searchBox->text().isEmpty() && levelFilter->currentIndex() == 0;
    if (isFiltersEmpty) {
        alertTable->insertRow(0);
        QDateTime time = QDateTime::fromSecsSinceEpoch(alert.timestamp);
        auto *itemTime = new QTableWidgetItem(time.toString("HH:mm:ss"));
        itemTime->setData(Qt::UserRole, 0);
        QString levelStr; int levelIdx = 3;
        switch(alert.level) {
        case Alert::Critical: levelStr="严重"; levelIdx=0; break;
        case Alert::High:     levelStr="高危"; levelIdx=1; break;
        case Alert::Medium:   levelStr="中危"; levelIdx=2; break;
        case Alert::Low:      levelStr="低危"; levelIdx=3; break;
        default:              levelStr="信息"; levelIdx=3; break;
        }
        auto *itemLevel = new QTableWidgetItem(levelStr);
        itemLevel->setForeground(AlertsStyle::getLevelColor(levelIdx));
        itemLevel->setFont(QFont("Inter", 15, QFont::Bold));

        auto *itemRule = new QTableWidgetItem(QString::fromStdString(alert.ruleName));
        auto *itemSrc = new QTableWidgetItem(QHostAddress(alert.sourceIp).toString());
        alertTable->setItem(0, 0, itemTime);
        alertTable->setItem(0, 1, itemLevel);
        alertTable->setItem(0, 2, itemRule);
        alertTable->setItem(0, 3, itemSrc);
    } else {
        refreshTable();
    }
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
        itemTime->setData(Qt::UserRole, (int)i);

        QString levelStr; int levelColorIdx = 3;
        switch(alert.level) {
            case Alert::Critical: levelStr="严重"; levelColorIdx=0; break;
            case Alert::High:     levelStr="高危"; levelColorIdx=1; break;
            case Alert::Medium:   levelStr="中危"; levelColorIdx=2; break;
            case Alert::Low:      levelStr="低危"; levelColorIdx=3; break;
            default:              levelStr="信息"; levelColorIdx=3; break;
        }
        auto *itemLevel = new QTableWidgetItem(levelStr);
        itemLevel->setForeground(AlertsStyle::getLevelColor(levelColorIdx));
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

void AlertsPage::onFilterChanged() {
    refreshTable();
}

void AlertsPage::onClearClicked() {
    alertsBuffer.clear();
    refreshTable();
    txtDetailContent->clear();
    lblDetailTitle->setText("选择告警查看详情");
}

void AlertsPage::onExportClicked() {
    if (alertsBuffer.empty()) {
        QMessageBox::information(this, "导出", "当前没有告警数据！");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "导出告警日志", "", "CSV 文件 (*.csv);;JSON 文件 (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    if (fileName.endsWith(".json", Qt::CaseInsensitive)) {
        QJsonArray jsonArray;
        for (const auto& alert : alertsBuffer) {
            QJsonObject obj;
            obj["timestamp"] = static_cast<qint64>(alert.timestamp);
            obj["rule"] = QString::fromStdString(alert.ruleName);
            obj["src"] = QHostAddress(alert.sourceIp).toString();
            obj["level"] = static_cast<int>(alert.level);
            obj["desc"] = QString::fromStdString(alert.description);
            jsonArray.append(obj);
        }
        out << QJsonDocument(jsonArray).toJson();
    } else {
        out << "\xEF\xBB\xBF";
        out << "Time,Level,Rule,Source IP,Description\n";
        for (const auto& alert : alertsBuffer) {
            QString levelStr = (alert.level == Alert::Critical) ? "Critical" :
                               (alert.level == Alert::High) ? "High" : "Normal";
            out << QDateTime::fromSecsSinceEpoch(alert.timestamp).toString("yyyy-MM-dd HH:mm:ss") << ","
                << levelStr << ","
                << QString::fromStdString(alert.ruleName) << ","
                << QHostAddress(alert.sourceIp).toString() << ",\""
                << QString::fromStdString(alert.description).replace("\"", "\"\"") << "\"\n";
        }
    }
    file.close();
    QMessageBox::information(this, "成功", "告警日志已导出！");
}

void AlertsPage::updateDetailView(const Alert& alert) {
    lblDetailTitle->setText(QString::fromStdString(alert.ruleName));
    QString html = AlertsStyle::getDetailHtml(
        QDateTime::fromSecsSinceEpoch(alert.timestamp).toString("yyyy-MM-dd HH:mm:ss"),
        QHostAddress(alert.sourceIp).toString(),
        QString::fromStdString(alert.description)
    );
    txtDetailContent->setHtml(html);
}