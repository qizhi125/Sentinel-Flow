#include "capture/impl/PcapCapture.h"
#include "engine/flow/SecurityEngine.h"
#include "engine/context/DatabaseManager.h"
#include "presentation/views/styles/ThemeManager.h"
#include "presentation/views/pages/ForensicPage.h"
#include "MainWindow.h"
#include "styles/global.h"
#include <QLabel>
#include <QFrame>
#include <iostream>
#include <QThread>
#include <QDebug>

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
    setWindowTitle("Sentinel-Flow v1.0 | 网络安全引擎");

    int idealThreads = std::thread::hardware_concurrency();
    int workerCount = (idealThreads > 4) ? 4 : (idealThreads > 2 ? idealThreads - 2 : 1);

    std::cout << "\n[CORE] v6.0 Hyper-Exchange Engine Initializing..." << std::endl;
    std::cout << "[CORE] Detected CPU Cores: " << idealThreads << std::endl;
    std::cout << "[CORE] Allocating Workers: " << workerCount << std::endl;

    SecurityEngine::instance().compileRules();
    std::cout << "[CORE] SecurityEngine Rules Compiled." << std::endl;

    DatabaseManager::instance().init("sentinel_data.db");

    for (int i = 0; i < workerCount; ++i) {
        auto* q = new sentinel::common::SPSCQueue<RawPacket>();
        workerQueues.push_back(q);
    }

    m_captureDriver = &PcapCapture::instance();
    m_captureDriver->init(workerQueues);

    auto devs = m_captureDriver->getDeviceList();

    for (int i = 0; i < workerCount; ++i) {
        auto* pipe = new PacketPipeline();

        pipe->setInputQueue(workerQueues[i]);
        pipe->setInspector(&SecurityEngine::instance());

        pipe->setCoreId((i + 1) % idealThreads);

        connect(pipe, &PacketPipeline::packetsProcessed, this, &MainWindow::onPacketsProcessed, Qt::QueuedConnection);
        connect(pipe, &PacketPipeline::threatDetected, this, &MainWindow::onThreatDetected, Qt::QueuedConnection);
        connect(pipe, &PacketPipeline::statsUpdated, this, &MainWindow::onStatsUpdated, Qt::QueuedConnection);

        pipelinePool.append(pipe);
        pipe->startPipeline();
    }

    dashboardPage = new DashboardPage(this);
    monitorPage   = new TrafficMonitorPage(this);
    alertsPage    = new AlertsPage(this);
    statsPage     = new StatisticsPage(this);
    forensicPage  = new ForensicPage(this);
    rulesPage     = new RulesPage(this);
    settingsPage  = new SettingsPage(this);

    setupUi();

    dashboardPage->addSystemLog(QString("v6.0 引擎就绪 (Workers: %1)").arg(workerCount), "SUCCESS");

    if (!devs.empty()) {
        std::string defaultDev = devs[0];
        QStringList args = QCoreApplication::arguments();
        int idx = args.indexOf("--interface");
        if (idx != -1 && idx + 1 < args.size()) {
            defaultDev = args[idx + 1].toStdString();
        }

        std::cout << "[CORE] Auto-starting capture on device: " << defaultDev << std::endl;
        m_captureDriver->start(defaultDev);
        dashboardPage->addSystemLog(QString("引擎启动: %1").arg(QString::fromStdString(defaultDev)), "INFO");
    } else {
        dashboardPage->addSystemLog("未检测到网络接口，引擎待机", "WARN");
        std::cerr << "[CORE] No network devices found!" << std::endl;
    }

    connect(settingsPage, &SettingsPage::captureInterfaceChanged, [this](const QString& iface){
        PcapCapture::instance().stop();
        PcapCapture::instance().start(iface.toStdString());
        dashboardPage->addSystemLog("捕获接口已切换: " + iface, "INFO");
    });

    connect(settingsPage, &SettingsPage::themeChanged, this, &MainWindow::onThemeChanged);

    QTimer *dashTimer = new QTimer(this);
    connect(dashTimer, &QTimer::timeout, [this](){
        if (!dashboardPage) return;
        int activeCount = 5;
        bool netOk = !pipelinePool.isEmpty() && pipelinePool.first()->isRunning();

        dashboardPage->updateSecurityStatus(totalAlertsSession, activeCount);
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

void MainWindow::onPacketsProcessed(QSharedPointer<QVector<ParsedPacket>> packets) {
    for (const auto& packet : *packets) {
        if (statsPage) statsPage->addPacket(packet);
        if (monitorPage) monitorPage->addPacket(packet);
    }
}

void MainWindow::onThreatDetected(const Alert& alert, const ParsedPacket& packet) {
    if (!alertsPage || !dashboardPage) return;

    totalAlertsSession++;

    alertsPage->addAlert(alert);

    QString severity;
    switch (alert.level) {
    case Alert::Critical: severity = "CRITICAL"; break;
    case Alert::High:     severity = "HIGH"; break;
    case Alert::Medium:   severity = "MEDIUM"; break;
    case Alert::Low:      severity = "LOW"; break;
    default:              severity = "INFO"; break;
    }

    QString srcIp = packet.getSrcIpStr();
    dashboardPage->triggerRadarAlert(srcIp, severity);
}

void MainWindow::onStatsUpdated(uint64_t bytes) {
    if (statsPage) statsPage->updateThroughput(bytes);
}

void MainWindow::onThemeChanged(bool isDark) {
    qDebug() << "MainWindow::onThemeChanged" << isDark;

    // 检查页面指针有效性
    QList<QWidget*> pages = {dashboardPage, monitorPage, alertsPage, statsPage, rulesPage, settingsPage};
    for (auto* page : pages) {
        if (!page) {
            qDebug() << "Page is null:" << page;
        } else if (!page->isWidgetType()) {
            qDebug() << "Page is not a valid widget:" << page;
        } else {
            qDebug() << "Page is valid:" << page->metaObject()->className() << page;
        }
    }

    // 应用主题调色板
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        ThemeManager::applyTheme(*app, isDark);
    }

    // 通知各页面刷新样式
    if (dashboardPage && dashboardPage->isWidgetType()) {
        dashboardPage->onThemeChanged();
    } else {
        qDebug() << "dashboardPage invalid, skipping";
    }

    if (statsPage && statsPage->isWidgetType()) {
        statsPage->onThemeChanged();
    } else {
        qDebug() << "statsPage invalid, skipping";
    }

    if (monitorPage && monitorPage->isWidgetType()) {
        monitorPage->onThemeChanged();
    } else {
        qDebug() << "monitorPage invalid, skipping";
    }

    if (alertsPage && alertsPage->isWidgetType()) {
        alertsPage->onThemeChanged();
    } else {
        qDebug() << "alertsPage invalid, skipping";
    }

    if (forensicPage && forensicPage->isWidgetType()) {
        forensicPage->onThemeChanged();
    } else {
        qDebug() << "forensicPage invalid, skipping";
    }

    if (rulesPage && rulesPage->isWidgetType()) {
        rulesPage->onThemeChanged();
    } else {
        qDebug() << "rulesPage invalid, skipping";
    }

    if (settingsPage && settingsPage->isWidgetType()) {
        // SettingsPage 本身是触发者，可能不需要刷新，但可保留
        // 如果需要刷新，可调用 settingsPage->onThemeChanged()，但当前未实现
    }

    this->update();
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
    contentStack->addWidget(forensicPage);
    contentStack->addWidget(rulesPage);
    contentStack->addWidget(settingsPage);
    contentStack->setCurrentIndex(0);

    QFrame *contentFrame = new QFrame(); contentFrame->setObjectName("ContentFrame");
    contentFrame->setStyleSheet("QFrame#ContentFrame { border-radius: 0px; border-left: 1px solid #333; }");
    QVBoxLayout *contentLayout = new QVBoxLayout(contentFrame); contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(contentStack);
    sidebar->setFixedWidth(220);
    mainLayout->addWidget(sidebar); mainLayout->addWidget(contentFrame);
}

void MainWindow::setupSidebar() {
    sidebar = new QWidget();
    sidebar->setObjectName("Sidebar");
    sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(20, 40, 20, 20);
    sidebarLayout->setSpacing(10);
    sidebarLayout->setAlignment(Qt::AlignTop);

    QLabel *logo = new QLabel("Sentinel");
    logo->setStyleSheet("font-size: 28px; padding-left: 10px; margin-bottom: 30px; font-weight: bold; color: #00BFA5;");
    sidebarLayout->addWidget(logo);

    auto createBtn = [](const QString &text, bool checked = false) {
        QPushButton *btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName("SidebarBtn");
        return btn;
    };

    btnDashboard = createBtn("📊  态势感知", true);
    btnMonitor   = createBtn("🔍  流量监控");
    btnAlerts    = createBtn("🛡️  安全告警");
    btnStats     = createBtn("📈  统计分析");
    btnForensics = createBtn("📂  离线取证");
    btnRules     = createBtn("⚖️  规则策略");
    btnSettings  = createBtn("⚙️  全局设置");

    sidebarLayout->addWidget(btnDashboard);
    sidebarLayout->addWidget(btnMonitor);
    sidebarLayout->addWidget(btnAlerts);
    sidebarLayout->addWidget(btnStats);
    sidebarLayout->addWidget(btnForensics);
    sidebarLayout->addWidget(btnRules);
    sidebarLayout->addStretch();
    sidebarLayout->addWidget(btnSettings);

    auto resetButtons = [this]() {
        btnDashboard->setChecked(false);
        btnMonitor->setChecked(false);
        btnAlerts->setChecked(false);
        btnStats->setChecked(false);
        btnForensics->setChecked(false);
        btnRules->setChecked(false);
        btnSettings->setChecked(false);
    };

    auto connectBtn = [this, resetButtons](QPushButton* btn, int index){
        connect(btn, &QPushButton::clicked, [this, resetButtons, btn, index](){
            contentStack->setCurrentIndex(index);
            resetButtons();
            btn->setChecked(true);
        });
    };

    connectBtn(btnDashboard, 0);
    connectBtn(btnMonitor, 1);
    connectBtn(btnAlerts, 2);
    connectBtn(btnStats, 3);
    connectBtn(btnForensics, 4);
    connectBtn(btnRules, 5);
    connectBtn(btnSettings, 6);
}