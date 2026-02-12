#pragma once
#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QPainter>
#include <QProgressBar>
#include <vector>

struct RadarBlip {
    int angle;
    float opacity;
    QColor color;
};

class RadarWidget : public QWidget {
    Q_OBJECT
public:
    explicit RadarWidget(QWidget *parent = nullptr);
    void addBlip(const QString& sourceIp, const QString& severity);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QTimer *scanTimer;
    int scanAngle = 0;
    std::vector<RadarBlip> blips;
};

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);
    void updateSystemMetrics();
    void addSystemLog(const QString& message, const QString& type = "INFO");
    void updateSecurityStatus(int totalAlerts, int activeModules);
    void updateServiceStatus(bool aiOk, bool dbOk, bool netOk);
    void triggerRadarAlert(const QString& sourceIp, const QString& severity);
    void setTheme(bool isDark);

private:
    void setupUi();
    double getRealCpuUsage();
    double getRealRamUsage();
    double getDiskUsage();
    unsigned long long prevIdle = 0;
    unsigned long long prevTotal = 0;

    QLabel *lblSafetyScore;
    QLabel *lblThreatLevel;
    QLabel *lblActiveProtections;

    RadarWidget *radarWidget;
    QListWidget *activityList;

    QLabel *lblServiceDB;
    QLabel *lblServiceAI;
    QLabel *lblServiceNet;

    QProgressBar *barCpu; QProgressBar *barRam; QProgressBar *barDisk;
    QLabel *lblCpuVal; QLabel *lblRamVal; QLabel *lblDiskVal;

    QTimer *monitorTimer;
};