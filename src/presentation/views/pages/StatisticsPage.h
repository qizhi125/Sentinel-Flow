#pragma once
#include "common/types/NetworkTypes.h"
#include <QtCharts/QChartView>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAreaSeries>
#include "ThemeablePage.h"
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <QElapsedTimer>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QScrollArea>

class StatisticsPage : public ThemeablePage {
    Q_OBJECT
public:
    explicit StatisticsPage(QWidget *parent = nullptr);
    void onThemeChanged() override;
    void addPacket(const ParsedPacket& packet);
    void updateThreatStats(const QString& type);
    void updateThroughput(uint64_t throughputBps);
    void setTheme(bool dark);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void updateMetrics();
    void refreshLocalIps();

    static constexpr int MAX_HISTORY = 60;

    struct ProtoBarWidgets {
        QLabel* name = nullptr;
        QProgressBar* bar = nullptr;
        QLabel* percent = nullptr;
    };

    QWidget *contentWidget = nullptr;
    QChartView *chartCanvas = nullptr;
    QChart *trendChart = nullptr;
    QSplineSeries *rxSeries = nullptr;
    QSplineSeries *txSeries = nullptr;
    QValueAxis *axisX = nullptr;
    QValueAxis *axisY = nullptr;

    QWidget *topHostsContainer = nullptr;
    QVBoxLayout *topHostsLayout = nullptr;

    QLabel *lblSpeedVal = nullptr;
    QLabel *lblTotalVal = nullptr;
    QLabel *lblPpsVal = nullptr;
    QLabel *lblThreatVal = nullptr;

    QProgressBar *barDDoS = nullptr;
    QProgressBar *barMalware = nullptr;
    QProgressBar *barPhishing = nullptr;
    QLabel *lblDDoSVal = nullptr;
    QLabel *lblMalwareVal = nullptr;
    QLabel *lblPhishingVal = nullptr;

    QTimer *refreshTimer = nullptr;
    QElapsedTimer lastUpdateTimer;

    QSet<uint32_t> localIps;

    uint64_t totalPackets = 0;
    uint64_t packetsInInterval = 0;
    uint64_t totalRxBytes = 0;
    uint64_t totalTxBytes = 0;
    uint64_t rxBytesInInterval = 0;
    uint64_t txBytesInInterval = 0;

    double smoothRxSpeed = 0.0;
    double smoothTxSpeed = 0.0;

    QMap<QString, uint64_t> protocolBytes;
    QMap<uint32_t, uint64_t> sourceIpBytes;
    QMap<QString, ProtoBarWidgets> protoBars;
    QMap<QString, int> threatCounts;

    QList<QPointF> rxBuffer;
    QList<QPointF> txBuffer;
};
