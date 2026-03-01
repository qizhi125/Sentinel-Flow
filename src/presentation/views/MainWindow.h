#pragma once
#include "capture/interface/ICaptureDriver.h"
#include "engine/pipeline/PacketPipeline.h"
#include "common/queues/SPSCQueue.h"
#include "common/types/NetworkTypes.h"
#include "pages/DashboardPage.h"
#include "pages/TrafficMonitorPage.h"
#include "pages/AlertsPage.h"
#include "pages/StatisticsPage.h"
#include "pages/RulesPage.h"
#include "pages/SettingsPage.h"
#include "pages/ForensicPage.h"
#include <QMainWindow>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QList>
#include <vector>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    //void onPacketsReady(QSharedPointer<QVector<ParsedPacket>> packets);
    void onPacketsProcessed(QSharedPointer<QVector<ParsedPacket>> packets);
    void onThreatDetected(const Alert& alert, const ParsedPacket& packet);
    void onStatsUpdated(uint64_t bytes);
    void onThemeChanged(bool isDark);

private:
    void setupUi();
    void setupSidebar();
    void initSystemCore(int workerCount);

    QWidget *centralWidget;
    QHBoxLayout *mainLayout;
    QWidget *sidebar;
    QVBoxLayout *sidebarLayout;
    QStackedWidget *contentStack;

    DashboardPage *dashboardPage;
    TrafficMonitorPage *monitorPage;
    AlertsPage *alertsPage;
    StatisticsPage *statsPage;
    RulesPage *rulesPage;
    SettingsPage *settingsPage;
    ForensicPage *forensicPage;

    QPushButton *btnDashboard;
    QPushButton *btnMonitor;
    QPushButton *btnAlerts;
    QPushButton *btnStats;
    QPushButton *btnRules;
    QPushButton *btnSettings;
    QPushButton *btnForensics;

    QList<PacketPipeline*> pipelinePool;
    std::vector<sentinel::capture::PacketQueue*> workerQueues;

    sentinel::capture::ICaptureDriver* m_captureDriver = nullptr;

    int totalAlertsSession = 0;
};