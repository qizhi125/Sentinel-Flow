#pragma once
#include <QtCharts/QChartView>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAreaSeries>
#include <QList>
#include <QPointF>

class TrafficWaveChart : public QChartView {
    Q_OBJECT
public:
    explicit TrafficWaveChart(QWidget *parent = nullptr);
    void pushData(double speedKB);
    void setTheme(bool isDark);

private:
    void setupChart();

    QChart *m_chart;
    QSplineSeries *m_series;
    QValueAxis *m_axisX;
    QValueAxis *m_axisY;

    QList<QPointF> m_dataBuffer;
    int m_maxDataPoints;
    double m_maxY = 10.0;
};