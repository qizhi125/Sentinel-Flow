
#include "capture/impl/PcapCapture.h"
#include "engine/context/DatabaseManager.h"
#include "presentation/views/styles/ThemeManager.h"
#include "MainWindow.h"
#include "styles/global.h"
#include <QLabel>
#include <QFrame>
#include <iostream>
#include <QThread>

QString alertLevelToString(Alert::Level level) {
    switch (level) {
        case Alert::Level::Critical: return "严重";
        case Alert::Level::High:     return "高危";
        case Alert::Level::Medium:   return "中危";
        case Alert::Level::Low:      return "低危";
        case Alert::Level::Info:     return "信息";
        default:                     return "未知";
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    resize(1280, 850);
    setWindowTitle("Sentinel-Flow v6.0 | 网络安全引擎");

    int idealThreads = std::thread::hardware_concurrency();
    int workerCount = (idealThreads > 4) ? 4 : (idealThreads > 2 ? idealThreads - 2 : 1);

    std::cout << "\n[CORE] v6.0 Hyper-Exchange Engine Initializing..." << std::endl;
    std::cout << "[CORE] Detected CPU Cores: " << idealThreads << std::endl;
    std::cout << "[CORE] Allocating Workers: " << workerCount << std::endl;

    DatabaseManager::instance().init();

    for (int i = 0; i < workerCount; ++i) {
        auto* q = new ThreadSafeQueue<RawPacket>();
        workerQueues.push_back(q);
    }

    PcapCapture::instance().init(workerQueues);

    for (int i = 0; i < workerCount; ++i) {
        auto* pipe = new PacketPipeline(this);
        pipe->setInputQueue(workerQueues[i]);

        // 信号槽连接 (多线程安全)
        connect(pipe, &PacketPipeline::packetsProcessed, this, &MainWindow::onPacketsProcessed);
        connect(pipe, &PacketPipeline::threatDetected, this, &MainWindow::onThreatDetected);
        connect(pipe, &PacketPipeline::statsUpdated, this, &MainWindow::onStatsUpdated);

        pipelinePool.append(pipe);
        pipe->startPipeline(); // 立即就绪
    }

    dashboardPage = new DashboardPage(this);
    monitorPage = new TrafficMonitorPage(this);
    alertsPage = new AlertsPage(this);
    statsPage = new StatisticsPage(this);
    rulesPage = new RulesPage(this);
    settingsPage = new SettingsPage(this);

    setupUi();

    dashboardPage->addSystemLog(QString("v6.0 引擎就绪 (Workers: %1)").arg(workerCount), "SUCCESS");

    auto devs = PcapCapture::getDeviceList();
    if (!devs.empty()) {
        std::string defaultDev = devs[0];
        std::cout << "[CORE] Auto-starting capture on default device: " << defaultDev << std::endl;

        // 1. 启动捕获
        PcapCapture::instance().start(defaultDev);

        // 2. 更新 UI 状态
        dashboardPage->addSystemLog(QString("自动启动引擎: %1").arg(QString::fromStdString(defaultDev)), "INFO");

        // 3. (可选) 同步设置页面的下拉框选项（如果 settingsPage 有公开接口设置当前索引，这里可以调用）
        // settingsPage->setCurrentInterface(defaultDev);
    } else {
        dashboardPage->addSystemLog("未检测到网络接口，引擎待机", "WARN");
        std::cerr << "[CORE] No network devices found!" << std::endl;
    }
    // ------------------------------------------------------------

    connect(settingsPage, &SettingsPage::captureInterfaceChanged, [this](const QString& iface){
        PcapCapture::instance().stop();
        PcapCapture::instance().start(iface.toStdString());
        dashboardPage->addSystemLog("捕获接口已切换: " + iface, "INFO");
    });

    QTimer *dashTimer = new QTimer(this);
    connect(dashTimer, &QTimer::timeout, [this](){
        if (!dashboardPage) return;
        int activeCount = 5;
        bool netOk = !pipelinePool.isEmpty() && pipelinePool.first()->isRunning();
        dashboardPage->updateSecurityStatus(totalAlertsSession * 2, activeCount);
        dashboardPage->updateServiceStatus(true, true, netOk);
    });
    dashTimer->start(1000);
}

MainWindow::~MainWindow() {
    PcapCapture::instance().stop();

    for (auto* pipe : pipelinePool) {
        pipe->stopPipeline();
    }

    for (auto* pipe : pipelinePool) {
        pipe->wait(500);
        if (pipe->isRunning()) {
            pipe->terminate();
        }
        delete pipe;
    }
    pipelinePool.clear();

    for (auto* q : workerQueues) {
        delete q;
    }
    workerQueues.clear();

    DatabaseManager::instance().shutdown();

    std::cout << "[CORE] System shutdown sequence completed." << std::endl;
}

void MainWindow::onPacketsProcessed(const QVector<ParsedPacket>& packets) {
    for (const auto& packet : packets) {
        if (statsPage) statsPage->addPacket(packet);
        if (monitorPage) monitorPage->addPacket(packet);
    }
}

void MainWindow::onThreatDetected(const Alert& alert, const ParsedPacket& packet) {
    if (!alertsPage || !dashboardPage) return;

    alertsPage->addAlert(alert);

    QString severity;
    switch (alert.level) {
    case Alert::Critical: severity = "Critical"; break;
    case Alert::High:     severity = "High"; break;
    case Alert::Medium:   severity = "Medium"; break;
    case Alert::Low:      severity = "Low"; break;
    default:              severity = "Info"; break;
    }

    QString srcIp = packet.getSrcIpStr();
    dashboardPage->triggerRadarAlert(srcIp, severity);
}

void MainWindow::onStatsUpdated(uint64_t bytes) {
    if (statsPage) statsPage->updateThroughput(bytes);
}

void MainWindow::setupUi() {
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupSidebar();
    contentStack = new QStackedWidget();
    contentStack->addWidget(dashboardPage);
    contentStack->addWidget(monitorPage);
    contentStack->addWidget(alertsPage);
    contentStack->addWidget(statsPage);
    contentStack->addWidget(rulesPage);
    contentStack->addWidget(settingsPage);
    contentStack->setCurrentIndex(0);

    connect(settingsPage, &SettingsPage::themeChanged, [this](bool isDark){
        if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
            ThemeManager::applyTheme(*app, isDark);
        }
        if (statsPage) statsPage->setTheme(isDark);

        if (dashboardPage) dashboardPage->setTheme(isDark);

        // 更新监控页面的波形图 (如果有的话)
        // if (monitorPage) monitorPage->updateTheme(isDark);

        this->update();
    });

    QFrame *contentFrame = new QFrame(); contentFrame->setObjectName("ContentFrame");
    contentFrame->setStyleSheet("QFrame#ContentFrame { border-radius: 0px; border-left: 1px solid #333; }");
    QVBoxLayout *contentLayout = new QVBoxLayout(contentFrame); contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(contentStack);
    sidebar->setFixedWidth(220);
    mainLayout->addWidget(sidebar); mainLayout->addWidget(contentFrame);
}

void MainWindow::setupSidebar() {
    sidebar = new QWidget(); sidebar->setStyleSheet("background-color: transparent;");
    sidebarLayout = new QVBoxLayout(sidebar); sidebarLayout->setContentsMargins(20, 40, 20, 20); sidebarLayout->setSpacing(10); sidebarLayout->setAlignment(Qt::AlignTop);
    QLabel *logo = new QLabel("Sentinel"); logo->setStyleSheet("font-size: 28px; padding-left: 10px; margin-bottom: 30px; font-weight: bold; color: #00BFA5;");
    sidebarLayout->addWidget(logo);
    auto createBtn = [](const QString &text, bool checked = false) {
        QPushButton *btn = new QPushButton(text); btn->setCheckable(true); btn->setChecked(checked); btn->setCursor(Qt::PointingHandCursor); btn->setObjectName("SidebarBtn"); return btn;
    };
    btnDashboard = createBtn("📊  态势感知", true);
    btnMonitor   = createBtn("🔍  流量监控");
    btnAlerts    = createBtn("🛡️  安全告警");
    btnStats     = createBtn("📈  统计分析");
    btnRules     = createBtn("⚖️  规则策略");
    btnSettings  = createBtn("⚙️  全局设置");
    sidebarLayout->addWidget(btnDashboard); sidebarLayout->addWidget(btnMonitor); sidebarLayout->addWidget(btnAlerts);
    sidebarLayout->addWidget(btnStats); sidebarLayout->addWidget(btnRules);
    sidebarLayout->addStretch(); sidebarLayout->addWidget(btnSettings);

    auto resetButtons = [this]() {
        btnDashboard->setChecked(false); btnMonitor->setChecked(false); btnAlerts->setChecked(false);
        btnStats->setChecked(false); btnRules->setChecked(false); btnSettings->setChecked(false);
    };
    auto connectBtn = [this, resetButtons](QPushButton* btn, int index){
        connect(btn, &QPushButton::clicked, [this, resetButtons, btn, index](){
            contentStack->setCurrentIndex(index); resetButtons(); btn->setChecked(true);
        });
    };
    connectBtn(btnDashboard, 0); connectBtn(btnMonitor, 1); connectBtn(btnAlerts, 2);
    connectBtn(btnStats, 3); connectBtn(btnRules, 4); connectBtn(btnSettings, 5);
}