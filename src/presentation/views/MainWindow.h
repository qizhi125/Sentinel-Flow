#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QList> // 🔥 引入 QList
#include <vector> // 🔥 引入 vector
#include "pages/DashboardPage.h"
#include "pages/TrafficMonitorPage.h"
#include "pages/AlertsPage.h"
#include "pages/StatisticsPage.h"
#include "pages/RulesPage.h"
#include "pages/SettingsPage.h"
#include "engine/pipeline/PacketPipeline.h" // 路径已更新
#include "common/queues/ThreadSafeQueue.h"  // 路径已更新
#include "common/types/NetworkTypes.h"      // 路径已更新

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onPacketsProcessed(const QVector<ParsedPacket>& packets);
    void onThreatDetected(const Alert& alert, const ParsedPacket& packet);
    void onStatsUpdated(uint64_t bytes);

private:
    void setupUi();
    void setupSidebar();

    QWidget *centralWidget;
    QHBoxLayout *mainLayout;
    QWidget *sidebar;
    QVBoxLayout *sidebarLayout;
    QStackedWidget *contentStack;

    // 页面指针
    DashboardPage *dashboardPage;
    TrafficMonitorPage *monitorPage;
    AlertsPage *alertsPage;
    StatisticsPage *statsPage;
    RulesPage *rulesPage;
    SettingsPage *settingsPage;

    // 侧边栏按钮
    QPushButton *btnDashboard;
    QPushButton *btnMonitor;
    QPushButton *btnAlerts;
    QPushButton *btnStats;
    QPushButton *btnRules;
    QPushButton *btnSettings;

    // 🔥 [核心升级] 线程池与队列组
    QList<PacketPipeline*> pipelinePool;
    std::vector<ThreadSafeQueue<RawPacket>*> workerQueues;

    int totalAlertsSession = 0;
};