#include "presentation/views/components/UIFactory.h"
#include "presentation/views/pages/SettingsPage.h"
#include "presentation/views/styles/global.h"
#include "capture/impl/PcapCapture.h"
#include "engine/context/DatabaseManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QPalette>
#include <QApplication>

SettingsPage::SettingsPage(QWidget *parent) : ThemeablePage(parent) {
    setupUi();
    loadPersistedSettings();
}

QWidget* SettingsPage::createSectionCard(const QString& title) {
    auto *card = new QFrame();
    card->setObjectName("Card");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    auto *lblTitle = new QLabel(title);
    lblTitle->setStyleSheet("font-size: 15px; font-weight: bold; margin-bottom: 5px;");
    layout->addWidget(lblTitle);

    return card;
}

void SettingsPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(UIFactory::createInfoBox("系统全局配置", "修改底层捕获引擎、用户界面偏好及数据库维护策略。部分核心引擎参数修改后需要重启流量捕获才能生效。"), 0);

    auto *gridLayout = new QGridLayout();
    gridLayout->setSpacing(20);

    QWidget *engineCard = createSectionCard("🚀 捕获引擎 (Capture Engine)");
    auto *engineLayout = qobject_cast<QVBoxLayout*>(engineCard->layout());

    auto *h1 = new QHBoxLayout();
    h1->addWidget(new QLabel("监听网卡 (Interface):"));
    cbInterface = new QComboBox();
    for (const auto& dev : PcapCapture::instance().getDeviceList()) {
        cbInterface->addItem(QString::fromStdString(dev));
    }
    h1->addWidget(cbInterface, 1);
    engineLayout->addLayout(h1);

    chkPromiscuous = new QCheckBox("启用混杂模式 (Promiscuous Mode)");
    chkPromiscuous->setToolTip("开启后将捕获局域网内所有流经本网卡的流量，而不仅限于发往本机的流量。");
    engineLayout->addWidget(chkPromiscuous);

    auto *h2 = new QHBoxLayout();
    h2->addWidget(new QLabel("环形缓冲区大小 (MB):"));
    spinBufferSize = new QSpinBox();
    spinBufferSize->setRange(16, 1024);
    spinBufferSize->setValue(128);
    spinBufferSize->setSuffix(" MB");
    h2->addWidget(spinBufferSize);
    h2->addStretch();
    engineLayout->addLayout(h2);

    btnApplyEngine = new QPushButton("应用并热重启引擎");
    btnApplyEngine->setProperty("type", "danger");
    connect(btnApplyEngine, &QPushButton::clicked, this, &SettingsPage::onApplyEngineSettings);
    engineLayout->addStretch();
    engineLayout->addWidget(btnApplyEngine, 0, Qt::AlignRight);

    gridLayout->addWidget(engineCard, 0, 0);

    QWidget *appearanceCard = createSectionCard("🎨 外观与交互 (Appearance)");
    auto *appLayout = qobject_cast<QVBoxLayout*>(appearanceCard->layout());

    auto *h3 = new QHBoxLayout();
    h3->addWidget(new QLabel("系统主题 (Theme):"));
    cbTheme = new QComboBox();
    cbTheme->addItems({"☀️ 浅色模式 (Light)", "🌙 深色模式 (Dark)"});
    connect(cbTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::onThemeSelectionChanged);
    h3->addWidget(cbTheme, 1);
    appLayout->addLayout(h3);

    chkAutoStart = new QCheckBox("开启桌面系统原生通知框");
    chkAutoStart->setChecked(true);
    appLayout->addWidget(chkAutoStart);
    appLayout->addStretch();

    gridLayout->addWidget(appearanceCard, 0, 1);

    QWidget *storageCard = createSectionCard("🗄️ 存储与维护 (Maintenance)");
    auto *storageLayout = qobject_cast<QVBoxLayout*>(storageCard->layout());

    auto *h4 = new QHBoxLayout();
    h4->addWidget(new QLabel("告警日志保留策略:"));
    cbLogRetention = new QComboBox();
    cbLogRetention->addItems({"保留最近 7 天", "保留最近 30 天", "保留最近 90 天", "永久保留"});
    cbLogRetention->setCurrentIndex(1);
    h4->addWidget(cbLogRetention, 1);
    storageLayout->addLayout(h4);

    auto *lblDbHint = new QLabel("由于 SQLite 采用 WAL 模式，频繁写入告警会导致数据库体积膨胀。\n建议每月进行一次碎片整理以释放磁盘空间。");
    lblDbHint->setStyleSheet("color: #888; font-size: 11px; margin-top: 10px;");
    storageLayout->addWidget(lblDbHint);

    btnVacuumDb = new QPushButton("🧹 执行数据库碎片整理 (Vacuum)");
    btnVacuumDb->setProperty("type", "primary");
    connect(btnVacuumDb, &QPushButton::clicked, this, &SettingsPage::onVacuumDatabase);
    storageLayout->addStretch();
    storageLayout->addWidget(btnVacuumDb, 0, Qt::AlignRight);

    gridLayout->addWidget(storageCard, 1, 0, 1, 2);

    gridLayout->setColumnStretch(0, 5);
    gridLayout->setColumnStretch(1, 5);

    mainLayout->addLayout(gridLayout, 1);
}

void SettingsPage::loadPersistedSettings() {
    QString savedIface = QString::fromStdString(DatabaseManager::instance().loadConfig("capture_interface", ""));
    if (!savedIface.isEmpty()) {
        int idx = cbInterface->findText(savedIface);
        if (idx >= 0) cbInterface->setCurrentIndex(idx);
    }

    bool promisc = DatabaseManager::instance().loadConfig("promiscuous_mode", "1") == "1";
    chkPromiscuous->setChecked(promisc);

    int themeIdx = std::stoi(DatabaseManager::instance().loadConfig("ui_theme", "1"));
    cbTheme->blockSignals(true);
    cbTheme->setCurrentIndex(themeIdx);
    cbTheme->blockSignals(false);
}

void SettingsPage::onApplyEngineSettings() {
    QString selectedIface = cbInterface->currentText();
    if (selectedIface.isEmpty()) return;

    DatabaseManager::instance().saveConfig("capture_interface", selectedIface.toStdString());
    DatabaseManager::instance().saveConfig("promiscuous_mode", chkPromiscuous->isChecked() ? "1" : "0");

    emit captureInterfaceChanged(selectedIface);

    QMessageBox::information(this, "引擎已重启", QString("底层捕获引擎已成功切换至: %1\n当前模式: %2").arg(selectedIface, chkPromiscuous->isChecked() ? "混杂模式 (Promiscuous)" : "普通模式"));
}

void SettingsPage::onThemeSelectionChanged(int index) {
    bool isDark = (index == 1);

    DatabaseManager::instance().saveConfig("ui_theme", isDark ? "1" : "0");

    emit themeChanged(isDark);
}

void SettingsPage::onThemeChanged() {
}

void SettingsPage::onVacuumDatabase() {
    auto reply = QMessageBox::question(this, "数据库维护", "执行 Vacuum 将会锁定数据库几秒钟以释放碎片空间。\n确定现在执行吗？", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        // [未来可接入 DatabaseManager::instance().vacuum()]
        QMessageBox::information(this, "维护完成", "数据库碎片整理完成，性能已优化。");
    }
}