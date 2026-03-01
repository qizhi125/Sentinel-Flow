#pragma once
#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QPainter>
#include <QProgressBar>
#include <QtCharts/QChartView>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QDateTimeAxis>
#include <deque>

struct ThreatEvent {
    qint64 timestamp;  // 毫秒
    QString severity;  // "CRITICAL", "HIGH", "MEDIUM", "LOW"
};

class ThreatTimelineWidget : public QChartView {
    Q_OBJECT
public:
    explicit ThreatTimelineWidget(QWidget *parent = nullptr);
    void addEvent(const ThreatEvent& event);
    void clear();
    void setTheme(bool isDark);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupChart();
    void updateAxisRange();

    QChart *m_chart;
    QScatterSeries *m_seriesCritical;
    QScatterSeries *m_seriesHigh;
    QScatterSeries *m_seriesMedium;
    QScatterSeries *m_seriesLow;
    QDateTimeAxis *m_axisX;
    QCategoryAxis *m_axisY;

    std::deque<ThreatEvent> m_events;
    static constexpr int MAX_EVENTS = 100;
    static constexpr int TIME_WINDOW_MS = 60000; // 1分钟
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

public slots:
    void onThemeChanged();

private:
    void setupUi();
    double getRealCpuUsage();
    double getRealRamUsage();
    double getDiskUsage();

    void calculateDynamicScore();
    std::deque<ThreatEvent> m_slidingWindowAlerts;
    static constexpr int SCORE_WINDOW_MS = 60000;

    double m_currentHealthScore = 100.0;

    unsigned long long prevIdle = 0;
    unsigned long long prevTotal = 0;

    QLabel *lblSafetyScore;
    QLabel *lblThreatLevel;
    QLabel *lblActiveProtections;

    ThreatTimelineWidget *threatTimeline;
    QTextEdit *activityConsole;

    QLabel *lblServiceDB;
    QLabel *lblServiceAI;
    QLabel *lblServiceNet;

    QProgressBar *barCpu; QProgressBar *barRam; QProgressBar *barDisk;
    QLabel *lblCpuVal; QLabel *lblRamVal; QLabel *lblDiskVal;

    QTimer *monitorTimer;
};