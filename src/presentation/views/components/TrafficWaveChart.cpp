#include "presentation/views/components/TrafficWaveChart.h"
#include <QDateTime>
#include <QPainter>
#include <QLinearGradient>
#include <QOpenGLWidget>

TrafficWaveChart::TrafficWaveChart(QWidget *parent) : QChartView(parent) {
    setupChart();
}

void TrafficWaveChart::setupChart() {
    m_chart = new QChart();
    m_series = new QSplineSeries();

    m_series->setPointsVisible(false);

    QPen pen(QColor("#00BFA5"));
    pen.setWidth(2);
    m_series->setPen(pen);

    m_chart->addSeries(m_series);

    QAreaSeries *area = new QAreaSeries(m_series);
    QLinearGradient grad(0, 0, 0, 1);
    grad.setCoordinateMode(QGradient::ObjectBoundingMode);
    grad.setColorAt(0.0, QColor(0, 191, 165, 80));
    grad.setColorAt(1.0, Qt::transparent);
    area->setBrush(grad);
    area->setPen(Qt::NoPen);
    m_chart->addSeries(area);

    m_chart->legend()->hide();
    m_chart->setTitle("");

    m_chart->setBackgroundBrush(Qt::NoBrush);
    m_chart->setPlotAreaBackgroundBrush(Qt::NoBrush);
    this->setBackgroundBrush(Qt::NoBrush);

    m_chart->setAnimationOptions(QChart::NoAnimation);

    m_axisX = new QValueAxis();
    m_axisX->setRange(0, 60);
    m_axisX->setLabelFormat("");
    m_axisX->setGridLineVisible(false);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_series->attachAxis(m_axisX);
    area->attachAxis(m_axisX);

    m_axisY = new QValueAxis();
    m_axisY->setLabelFormat("%.0f");
    m_axisY->setRange(0, 100);
    m_axisY->setGridLineColor(QColor("#333"));
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_series->attachAxis(m_axisY);
    area->attachAxis(m_axisY);

    this->setChart(m_chart);
    this->setRenderHint(QPainter::Antialiasing);

    m_maxDataPoints = 61;

    m_dataBuffer.clear();
    for(int i=0; i<m_maxDataPoints; ++i) {
        m_dataBuffer.append(QPointF(i, 0));
    }
    m_series->replace(m_dataBuffer);
}

void TrafficWaveChart::pushData(double speedKB) {
    double currentMax = 0.0;
    for (int i = 0; i < m_dataBuffer.size() - 1; ++i) {
        m_dataBuffer[i].setY(m_dataBuffer[i + 1].y());
        if(m_dataBuffer[i].y() > currentMax) currentMax = m_dataBuffer[i].y();
    }

    if (!m_dataBuffer.isEmpty()) {
        m_dataBuffer.last().setY(speedKB);
        if(speedKB > currentMax) currentMax = speedKB;
    }

    if (currentMax > m_maxY || currentMax < m_maxY * 0.5) {
        m_maxY = (currentMax < 10.0) ? 10.0 : currentMax * 1.2;
        m_axisY->setRange(0, m_maxY);
    }

    m_series->replace(m_dataBuffer);
}

void TrafficWaveChart::setTheme(bool isDark) {
    if (isDark) {
        m_axisY->setLabelsColor(QColor("#888"));
        m_axisY->setGridLineColor(QColor("#333"));
        m_series->setPen(QPen(QColor("#00BFA5"), 2));
    } else {
        m_axisY->setLabelsColor(QColor("#333"));
        m_axisY->setGridLineColor(QColor("#DDD"));
        m_series->setPen(QPen(QColor("#009688"), 2));
    }
}