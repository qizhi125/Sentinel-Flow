#include "presentation/views/pages/StatisticsPage.h"
#include "presentation/views/styles/StatisticsStyle.h"
#include "common/utils/StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDateTime>
#include <QFrame>
#include <QEvent>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <QNetworkInterface>
#include <QDebug>

StatisticsPage::StatisticsPage(QWidget *parent) : ThemeablePage(parent) {
    refreshLocalIps();
    setupUi();
    lastUpdateTimer.start();
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &StatisticsPage::updateMetrics);
    refreshTimer->start(1000);
}

static QWidget* createMetricCard(const QString &title, const QString &emoji, QLabel **outLabel, const QString &color) {
    auto *card = new QFrame();
    card->setObjectName("Card");
    card->setStyleSheet(QString("#Card { border-left: 4px solid %1; }").arg(color));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 20, 20, 20);

    auto *lblTitle = new QLabel(emoji + " " + title);
    lblTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));

    *outLabel = new QLabel("-");
    (*outLabel)->setProperty("role", "value");
    (*outLabel)->setStyleSheet("font-size: 26px; font-weight: 600; background: transparent;");
    (*outLabel)->setTextFormat(Qt::RichText);

    layout->addWidget(lblTitle);
    layout->addSpacing(10);
    layout->addWidget(*outLabel);
    layout->addStretch();
    return card;
}

static QFrame* createSectionFrame(const QString &title, QVBoxLayout **outLayout, QWidget* rightWidget = nullptr) {
    auto *frame = new QFrame();
    frame->setObjectName("Card");

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(15);
    auto *headerWidget = new QWidget();
    headerWidget->setStyleSheet("background: transparent;");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto *lblTitle = new QLabel(title);
    lblTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));

    headerLayout->addWidget(lblTitle);
    headerLayout->addStretch();
    if (rightWidget) headerLayout->addWidget(rightWidget);
    layout->addWidget(headerWidget);
    *outLayout = layout;
    return frame;
}

static QWidget* createLegendItem(const QString& text, const QString& color) {
    auto* w = new QWidget();
    w->setStyleSheet("background: transparent;");
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(6);
    auto* box = new QFrame();
    box->setFixedSize(12, 3);
    box->setStyleSheet(QString("border-radius: 2px; background-color: %1;").arg(color));
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet("font-size: 13px; font-weight: bold; background: transparent; color: palette(text);");
    l->addWidget(box);
    l->addWidget(lbl); return w;
}

void StatisticsPage::setupUi() {
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(StatisticsStyle::ScrollArea);
    contentWidget = new QWidget();
    contentWidget->setStyleSheet("background: transparent;");
    auto *mainLayout = new QVBoxLayout(contentWidget);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(25);

    auto *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(20);
    cardsLayout->addWidget(createMetricCard("实时速率 (EMA)", "🚀", &lblSpeedVal, StatisticsStyle::ColorRate));
    cardsLayout->addWidget(createMetricCard("总流量 (Total)", "💾", &lblTotalVal, StatisticsStyle::ColorTotal));
    cardsLayout->addWidget(createMetricCard("PPS (包/秒)", "⚡", &lblPpsVal, StatisticsStyle::ColorPPS));
    cardsLayout->addWidget(createMetricCard("累计威胁", "🛡️", &lblThreatVal, StatisticsStyle::ColorThreat));
    mainLayout->addLayout(cardsLayout);

    auto* legendWidget = new QWidget();
    auto* legendLayout = new QHBoxLayout(legendWidget);
    legendLayout->setContentsMargins(0,0,0,0);
    legendLayout->setSpacing(15);
    legendLayout->addWidget(createLegendItem("⬇️ 下载", StatisticsStyle::ColorRate));
    legendLayout->addWidget(createLegendItem("⬆️ 上传", StatisticsStyle::ColorPPS));

    QVBoxLayout *chartLayout;
    QFrame *chartFrame = createSectionFrame("流量趋势 (60秒)", &chartLayout, legendWidget);

    // 🚀 核心修复：还原 QChartView 及其透明背景设置
    chartCanvas = new QChartView();
    chartCanvas->setFixedHeight(360);
    chartCanvas->setStyleSheet("background: transparent; border: none;");
    chartCanvas->setBackgroundBrush(Qt::NoBrush);
    chartCanvas->setRenderHint(QPainter::Antialiasing);

    trendChart = new QChart();
    trendChart->setBackgroundBrush(Qt::NoBrush);
    trendChart->setPlotAreaBackgroundBrush(Qt::NoBrush);
    trendChart->legend()->hide();
    trendChart->setMargins(QMargins(0, 0, 0, 0));

    rxSeries = new QSplineSeries();
    txSeries = new QSplineSeries();

    rxSeries->setPen(QPen(QColor(StatisticsStyle::ColorRate), 2));
    txSeries->setPen(QPen(QColor(StatisticsStyle::ColorPPS), 2));

    trendChart->addSeries(rxSeries);
    trendChart->addSeries(txSeries);

    QAreaSeries *rxArea = new QAreaSeries(rxSeries);
    QLinearGradient rxGrad(0, 0, 0, 1);
    rxGrad.setCoordinateMode(QGradient::ObjectBoundingMode);
    rxGrad.setColorAt(0, QColor(0, 191, 165, 80));
    rxGrad.setColorAt(1, Qt::transparent);
    rxArea->setBrush(rxGrad);
    rxArea->setPen(Qt::NoPen);
    trendChart->addSeries(rxArea);

    QAreaSeries *txArea = new QAreaSeries(txSeries);
    QLinearGradient txGrad(0, 0, 0, 1);
    txGrad.setCoordinateMode(QGradient::ObjectBoundingMode);
    txGrad.setColorAt(0, QColor(155, 89, 182, 80));
    txGrad.setColorAt(1, Qt::transparent);
    txArea->setBrush(txGrad);
    txArea->setPen(Qt::NoPen);
    trendChart->addSeries(txArea);

    axisX = new QValueAxis();
    axisX->setRange(0, MAX_HISTORY - 1);
    axisX->setLabelsVisible(false);
    axisX->setGridLineVisible(false);

    axisY = new QValueAxis();
    axisY->setRange(0, 10);
    axisY->setLabelFormat("%.1f");
    axisY->setLabelsColor(QColor(g_isDarkMode ? "#888888" : "#666666"));
    axisY->setGridLineColor(QColor(g_isDarkMode ? "#333333" : "#E0E0E0"));

    trendChart->addAxis(axisX, Qt::AlignBottom);
    trendChart->addAxis(axisY, Qt::AlignLeft);

    rxSeries->attachAxis(axisX); rxSeries->attachAxis(axisY);
    txSeries->attachAxis(axisX); txSeries->attachAxis(axisY);
    rxArea->attachAxis(axisX); rxArea->attachAxis(axisY);
    txArea->attachAxis(axisX); txArea->attachAxis(axisY);

    chartCanvas->setChart(trendChart);
    chartLayout->addWidget(chartCanvas);

    // 🚀 初始化平移缓冲区
    for (int i = 0; i < MAX_HISTORY; ++i) {
        rxBuffer.append(QPointF(i, 0));
        txBuffer.append(QPointF(i, 0));
    }
    rxSeries->replace(rxBuffer);
    txSeries->replace(txBuffer);

    mainLayout->addWidget(chartFrame);

    auto *splitLayout = new QHBoxLayout();
    splitLayout->setSpacing(20);
    QVBoxLayout *protoLayout;
    QFrame *protoFrame = createSectionFrame("协议分布", &protoLayout);
    auto *protoListContainer = new QWidget();
    protoListContainer->setStyleSheet("background: transparent; border: none;");
    auto *pLayout = new QVBoxLayout(protoListContainer);
    pLayout->setContentsMargins(0,0,0,0);
    pLayout->setSpacing(15);
    QStringList protos = {"TCP", "UDP", "TLS", "HTTP", "ICMP"};
    for (const auto& p : protos) {
        auto *row = new QWidget();
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0,0,0,0);
        auto *name = new QLabel(p);
        name->setFixedWidth(50);
        name->setStyleSheet("font-weight: bold; background: transparent;");
        auto *bar = new QProgressBar();
        bar->setFixedHeight(8);
        bar->setTextVisible(false);
        bar->setRange(0, 100);
        bar->setValue(0);

        QString col = StatisticsStyle::ColorDefault;
        if (p == "TCP") col = StatisticsStyle::ColorTCP;
        else if (p == "UDP") col = StatisticsStyle::ColorUDP;
        else if (p == "TLS") col = StatisticsStyle::ColorTLS;
        else if (p == "HTTP") col = StatisticsStyle::ColorHTTP;

        bar->setStyleSheet(QString("QProgressBar { background: %1; border: none; border-radius: 4px; } QProgressBar::chunk { background: %2; border-radius: 4px; }")
                           .arg(g_isDarkMode ? "#333333" : "#E0E0E0", col));

        auto *percent = new QLabel("0%");
        percent->setFixedWidth(40);
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent->setStyleSheet("font-family: 'JetBrains Mono'; font-weight: 800; background: transparent;");
        rowLay->addWidget(name);
        rowLay->addWidget(bar);
        rowLay->addWidget(percent);
        pLayout->addWidget(row);
        protoBars[p] = {name, bar, percent};
    }
    pLayout->addStretch();
    protoLayout->addWidget(protoListContainer);
    splitLayout->addWidget(protoFrame, 1);

    QVBoxLayout *hostLayout;
    QFrame *hostFrame = createSectionFrame("活跃主机(Top 5)", &hostLayout);
    topHostsContainer = new QWidget();
    topHostsContainer->setStyleSheet("background: transparent; border: none;");
    topHostsLayout = new QVBoxLayout(topHostsContainer);
    topHostsLayout->setContentsMargins(0,0,0,0);
    topHostsLayout->setSpacing(8);

    auto *lblEmpty = new QLabel("Waiting for IPv4 traffic...");
    lblEmpty->setObjectName("lblEmpty");
    lblEmpty->setStyleSheet("font-style: italic; background: transparent;");
    topHostsLayout->addWidget(lblEmpty);

    for (int i = 0; i < 5; ++i) {
        auto *r = new QWidget();
        r->setObjectName(QString("HostRow_%1").arg(i));
        auto *rl = new QHBoxLayout(r);
        rl->setContentsMargins(0,0,0,0);
        rl->setSpacing(10);

        auto *ip = new QLabel("N/A");
        ip->setObjectName("lblIp");
        ip->setStyleSheet("font-family: 'JetBrains Mono'; background: transparent; font-weight: 500;");
        ip->setFixedWidth(130);

        auto *b = new QProgressBar();
        b->setObjectName("progBar");
        b->setFixedHeight(6);
        b->setTextVisible(false);
        b->setRange(0, 100);
        b->setValue(0);
        b->setStyleSheet(QString("QProgressBar { background: %1; border: none; border-radius: 3px; } QProgressBar::chunk { background: #00BFA5; border-radius: 3px; }")
                         .arg(g_isDarkMode ? "#333333" : "#E0E0E0"));

        auto *n = new QLabel("0 B");
        n->setObjectName("lblBytes");
        n->setStyleSheet("font-family: 'JetBrains Mono'; font-weight: 800; background: transparent;");
        n->setAlignment(Qt::AlignRight);
        n->setFixedWidth(80);

        rl->addWidget(ip);
        rl->addWidget(b);
        rl->addWidget(n);

        r->setVisible(false);
        topHostsLayout->addWidget(r);
    }

    hostLayout->addWidget(topHostsContainer);
    hostLayout->addStretch();
    splitLayout->addWidget(hostFrame, 1);
    mainLayout->addLayout(splitLayout);

    QVBoxLayout *threatLayout;
    QFrame *threatFrame = createSectionFrame("威胁类型分布", &threatLayout);
    auto *statBox = new QWidget();
    auto *statL = new QHBoxLayout(statBox); statL->setSpacing(30);

    auto createThreatBar = [](const QString& name, const QString& col, QProgressBar** outBar, QLabel** outVal) -> QWidget* {
        auto *w = new QWidget();
        auto *vL = new QVBoxLayout(w); vL->setContentsMargins(0,0,0,0);
        auto *h = new QHBoxLayout();
        auto *n = new QLabel(name);
        n->setStyleSheet("font-weight: bold; background: transparent;");
        *outVal = new QLabel("0 Events");
        (*outVal)->setStyleSheet("font-weight: bold; background: transparent;");

        h->addWidget(n);
        h->addStretch();
        h->addWidget(*outVal);
        *outBar = new QProgressBar();
        (*outBar)->setFixedHeight(8);
        (*outBar)->setTextVisible(false);
        (*outBar)->setStyleSheet(QString("QProgressBar { background: %1; border: none; border-radius: 4px; } QProgressBar::chunk { background: %2; border-radius: 4px; }")
                                 .arg(g_isDarkMode ? "#333333" : "#E0E0E0", col));
        vL->addLayout(h);
        vL->addWidget(*outBar);
        return w;
    };

    statL->addWidget(createThreatBar("DDoS Attacks", StatisticsStyle::ColorDDoS, &barDDoS, &lblDDoSVal));
    statL->addWidget(createThreatBar("Malware / C2", StatisticsStyle::ColorMalware, &barMalware, &lblMalwareVal));
    statL->addWidget(createThreatBar("Phishing / Spam", StatisticsStyle::ColorPhishing, &barPhishing, &lblPhishingVal));
    threatLayout->addWidget(statBox);
    mainLayout->addWidget(threatFrame);

    mainLayout->addStretch();
    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);
}

void StatisticsPage::refreshLocalIps() {
    localIps.clear();
    const QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : list) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
            localIps.insert(addr.toIPv4Address());
        }
    }
    localIps.insert(QHostAddress("127.0.0.1").toIPv4Address());
}

void StatisticsPage::addPacket(const ParsedPacket& packet) {
    totalPackets++;
    packetsInInterval++;

    if (localIps.contains(packet.srcIp)) {
        totalTxBytes += packet.totalLen;
        txBytesInInterval += packet.totalLen;
    } else {
        totalRxBytes += packet.totalLen;
        rxBytesInInterval += packet.totalLen;
    }

    protocolBytes[packet.protocol] += packet.totalLen;

    if (sourceIpBytes.size() < 10000 || sourceIpBytes.contains(packet.srcIp)) {
        sourceIpBytes[packet.srcIp] += packet.totalLen;
    }
}

void StatisticsPage::updateThreatStats(const QString& type) {
    threatCounts[type]++;
    int maxVal = 1;
    for(auto v : threatCounts)
        if(v > maxVal) maxVal = v;

    int vDDoS = threatCounts.value("DDoS", 0);
    int vMalware = threatCounts.value("Malware", 0);
    int vPhish = threatCounts.value("Phishing", 0);

    barDDoS->setValue((int)((double)vDDoS / maxVal * 100));
    lblDDoSVal->setText(QString("%1 Events").arg(vDDoS));
    barMalware->setValue((int)((double)vMalware / maxVal * 100));
    lblMalwareVal->setText(QString("%1 Events").arg(vMalware));
    barPhishing->setValue((int)((double)vPhish / maxVal * 100));
    lblPhishingVal->setText(QString("%1 Events").arg(vPhish));
    lblThreatVal->setText(QString::number(vDDoS + vMalware + vPhish));
}

void StatisticsPage::updateThroughput(uint64_t bytesDelta) {
    Q_UNUSED(bytesDelta);
}

void StatisticsPage::setTheme(bool isDark) {
    if (axisY) {
        axisY->setLabelsColor(QColor(isDark ? "#888888" : "#666666"));
        axisY->setGridLineColor(QColor(isDark ? "#333333" : "#E0E0E0"));
    }
}

void StatisticsPage::onThemeChanged() {
    setTheme(g_isDarkMode);
}

void StatisticsPage::updateMetrics() {
    qint64 elapsedMs = lastUpdateTimer.restart(); if (elapsedMs < 1) elapsedMs = 1;
    double elapsedSec = static_cast<double>(elapsedMs) / 1000.0;
    double rxBps = static_cast<double>(rxBytesInInterval) / elapsedSec;
    double txBps = static_cast<double>(txBytesInInterval) / elapsedSec;

    const double alpha = 0.2;
    smoothRxSpeed = smoothRxSpeed * (1.0 - alpha) + rxBps * alpha;
    smoothTxSpeed = smoothTxSpeed * (1.0 - alpha) + txBps * alpha;

    double rxMbps = (smoothRxSpeed * 8.0) / 1e6;
    double txMbps = (smoothTxSpeed * 8.0) / 1e6;

    double maxV = 0.1;
    for (int i = 0; i < MAX_HISTORY - 1; ++i) {
        rxBuffer[i].setY(rxBuffer[i+1].y());
        txBuffer[i].setY(txBuffer[i+1].y());
        maxV = std::max({maxV, rxBuffer[i].y(), txBuffer[i].y()});
    }
    rxBuffer.last().setY(rxMbps);
    txBuffer.last().setY(txMbps);
    maxV = std::max({maxV, rxMbps, txMbps});

    axisY->setRange(0, maxV * 1.2);
    rxSeries->replace(rxBuffer);
    txSeries->replace(txBuffer);

    lblSpeedVal->setText(StatisticsStyle::formatSpeedHtml(StringUtils::formatBytes(smoothRxSpeed), StringUtils::formatBytes(smoothTxSpeed)));
    lblTotalVal->setText(StatisticsStyle::formatTotalHtml(StringUtils::formatBytes((double)totalRxBytes), StringUtils::formatBytes((double)totalTxBytes)));

    lblPpsVal->setText(QString::number((int)(packetsInInterval/elapsedSec)));
    rxBytesInInterval = 0;
    txBytesInInterval = 0;
    packetsInInterval = 0;

    uint64_t totalP = 0;
    for(auto it=protoBars.begin(); it!=protoBars.end(); ++it)
        totalP += protocolBytes.value(it.key(), 0);
    if(totalP > 0) {
        for(auto it=protoBars.begin(); it!=protoBars.end(); ++it) {
            uint64_t v = protocolBytes.value(it.key(), 0);
            int p = (int)((double)v/totalP * 100);
            it.value().bar->setValue(p);
            it.value().percent->setText(QString::number(p)+"%");
        }
    }

    QVector<QPair<uint64_t, uint32_t>> sorted;
    for(auto it = sourceIpBytes.begin(); it != sourceIpBytes.end(); ++it) {
        sorted.push_back({it.value(), it.key()});
    }

    if (sorted.size() > 5) {
        std::partial_sort(sorted.begin(), sorted.begin() + 5, sorted.end(), std::greater<QPair<uint64_t, uint32_t>>());
        sorted.resize(5);
    } else {
        std::sort(sorted.begin(), sorted.end(), std::greater<QPair<uint64_t, uint32_t>>());
    }

    uint64_t maxVHost = sorted.isEmpty() ? 1 : sorted.first().first;

    QLabel* lblEmptyPtr = topHostsContainer->findChild<QLabel*>("lblEmpty");
    if (lblEmptyPtr) lblEmptyPtr->setVisible(sorted.isEmpty());

    for (int i = 0; i < 5; ++i) {
        QWidget* row = topHostsContainer->findChild<QWidget*>(QString("HostRow_%1").arg(i));
        if (!row) continue;

        if (i < sorted.size()) {
            const auto& p = sorted[i];
            row->findChild<QLabel*>("lblIp")->setText(QHostAddress(p.second).toString());
            row->findChild<QProgressBar*>("progBar")->setValue((int)((double)p.first / maxVHost * 100));
            row->findChild<QLabel*>("lblBytes")->setText(StringUtils::formatBytes((double)p.first));
            row->setVisible(true);
        } else {
            row->setVisible(false);
        }
    }

    static int metricsTick = 0;
    if (++metricsTick >= 60) {
        sourceIpBytes.clear();
        metricsTick = 0;
    }
}

bool StatisticsPage::eventFilter(QObject *w, QEvent *e) {
    return QWidget::eventFilter(w, e);
}