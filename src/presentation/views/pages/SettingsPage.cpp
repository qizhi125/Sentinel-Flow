#include "presentation/views/pages/SettingsPage.h"
#include "presentation/views/styles/SettingsStyle.h"
#include "capture/impl/PcapCapture.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QFrame>
#include <QApplication>
#include <QGroupBox>

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    setupUi();
    populateInterfaces();
}

void SettingsPage::addSeparator(QVBoxLayout* layout) {
    auto *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(SettingsStyle::Separator);
    layout->addWidget(line);
}

void SettingsPage::addSectionTitle(QVBoxLayout* layout, const QString& title) {
    auto *lbl = new QLabel(title);
    lbl->setStyleSheet(SettingsStyle::SectionTitle);
    layout->addWidget(lbl);
}

void SettingsPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(SettingsStyle::ScrollArea);

    auto *contentWidget = new QWidget();
    contentWidget->setStyleSheet(SettingsStyle::ContentWidget);

    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(40, 30, 40, 30);
    contentLayout->setSpacing(15);

    // --- 1. 核心引擎配置 ---
    addSectionTitle(contentLayout, "核心引擎配置");

    auto *formCore = new QFormLayout();
    formCore->setSpacing(15);

    comboInterface = new QComboBox();
    comboInterface->setMinimumWidth(300);
    formCore->addRow(new QLabel("监听接口:", contentWidget), comboInterface);

    chkPromiscuous = new QCheckBox("开启混杂模式");
    chkPromiscuous->setStyleSheet(SettingsStyle::getCheckBox());
    chkPromiscuous->setChecked(true);
    formCore->addRow("", chkPromiscuous);

    editGlobalBpf = new QLineEdit();
    editGlobalBpf->setPlaceholderText("例如: tcp port 80");
    editGlobalBpf->setStyleSheet(SettingsStyle::Input);
    formCore->addRow(new QLabel("BPF 过滤器:", contentWidget), editGlobalBpf);

    contentLayout->addLayout(formCore);

    // 重启引擎按钮 (限制宽度)
    auto *btnLayoutCore = new QHBoxLayout();
    btnLayoutCore->addStretch();
    btnRestartEngine = new QPushButton("应用配置并重启引擎");
    btnRestartEngine->setFixedWidth(200);
    btnRestartEngine->setStyleSheet(SettingsStyle::BtnPrimary);
    connect(btnRestartEngine, &QPushButton::clicked, this, &SettingsPage::onApplyCoreClicked);
    contentLayout->addWidget(btnRestartEngine);

    addSeparator(contentLayout);

    // --- 2. 显示与监控 ---
    addSectionTitle(contentLayout, "显示与监控");
    auto *formDisplay = new QFormLayout();
    formDisplay->setSpacing(15);

    spinRefreshRate = new QSpinBox();
    spinRefreshRate->setRange(100, 5000);
    spinRefreshRate->setValue(1000);
    spinRefreshRate->setSuffix(" ms");
    formDisplay->addRow(new QLabel("刷新频率:", contentWidget), spinRefreshRate);

    comboTheme = new QComboBox();
    comboTheme->addItem("深色模式 (Dark)", QVariant(true));
    comboTheme->addItem("浅色模式 (Light)", QVariant(false));
    comboTheme->setMinimumWidth(200);
    connect(comboTheme, &QComboBox::currentIndexChanged, [this](int index){
        bool isDark = comboTheme->itemData(index).toBool();
        emit themeChanged(isDark);
    });
    formDisplay->addRow(new QLabel("界面主题:", contentWidget), comboTheme);

    contentLayout->addLayout(formDisplay);

    addSeparator(contentLayout);

    // --- 3. 数据存储 ---
    addSectionTitle(contentLayout, "数据存储");

    auto *lblRetention = new QLabel("数据保留策略:", contentWidget);
    contentLayout->addWidget(lblRetention);

    auto *retainLayout = new QHBoxLayout();
    grpRetention = new QButtonGroup(this);
    auto addRetainOpt = [&](const QString& text, int id, bool checked=false) {
        auto *rb = new QRadioButton(text);
        rb->setStyleSheet(SettingsStyle::getRadioButton());
        rb->setChecked(checked);
        grpRetention->addButton(rb, id);
        retainLayout->addWidget(rb);
    };
    addRetainOpt("1 天", 1);
    addRetainOpt("3 天", 3);
    addRetainOpt("7 天", 7, true);
    addRetainOpt("永久", -1);
    retainLayout->addStretch();
    contentLayout->addLayout(retainLayout);
    connect(grpRetention, &QButtonGroup::idClicked, this, &SettingsPage::onRetentionToggled);

    auto *storeLayout = new QHBoxLayout();
    editSavePath = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    editSavePath->setReadOnly(true);
    editSavePath->setStyleSheet(SettingsStyle::Input);
    storeLayout->addWidget(editSavePath);

    // 清空按钮：鲜红色，宽度与上方对齐
    btnClearDb = new QPushButton("🗑 清空所有历史数据");
    btnClearDb->setFixedWidth(200);
    btnClearDb->setStyleSheet(SettingsStyle::BtnDanger);
    connect(btnClearDb, &QPushButton::clicked, this, &SettingsPage::onClearDataClicked);
    storeLayout->addWidget(btnClearDb);

    contentLayout->addLayout(storeLayout);

    contentLayout->addStretch();
    scrollArea->setWidget(contentWidget);
    mainLayout->addWidget(scrollArea);
}

void SettingsPage::populateInterfaces() {
    comboInterface->clear();
    std::vector<std::string> devs = PcapCapture::getDeviceList();
    if (devs.empty()) {
        comboInterface->addItem("❌ 未检测到网卡");
        comboInterface->setEnabled(false);
    } else {
        for(const auto& d : devs) {
            QString name = QString::fromStdString(d);
            comboInterface->addItem("🔌 " + name, name);
        }
        comboInterface->setEnabled(true);
    }
}

void SettingsPage::onApplyCoreClicked() {
    QString iface = comboInterface->currentData().toString();
    QString bpf = editGlobalBpf->text();
    emit captureInterfaceChanged(iface);
    QMessageBox::information(this, "配置已应用", QString("核心引擎正在重启...\n\n监听接口: %1\nBPF规则: %2").arg(iface, bpf.isEmpty() ? "无" : bpf));
}

void SettingsPage::onClearDataClicked() {
    auto reply = QMessageBox::question(this, "高危操作", "确定要清空所有历史数据吗？\n此操作不可恢复！", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QMessageBox::information(this, "完成", "历史数据已清空。");
    }
}

void SettingsPage::onRetentionToggled(int id) {
    emit dataRetentionChanged(id);
}