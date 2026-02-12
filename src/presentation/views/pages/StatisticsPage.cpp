#include "presentation/views/pages/StatisticsPage.h"
#include "presentation/views/styles/StatisticsStyle.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QFrame>
#include <QEvent>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <QNetworkInterface>

StatisticsPage::StatisticsPage(QWidget *parent) : QWidget(parent) {
refreshLocalIps();
    setupUi();
    lastUpdateTimer.start();
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &StatisticsPage::updateMetrics);
    refreshTimer->start(1000);
}

QString StatisticsPage::formatBytes(double bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int i = 0;
    while (bytes >= 1024.0 && i < 5) { bytes /= 1024.0; i++; }
    return QString::number(bytes, 'f', 2) + " " + units[i];
}

static QWidget* createMetricCard(const QString &title, const QString &emoji, QLabel **outLabel, const QString &color) {
    auto *card = new QFrame();
    card->setObjectName("Card");
    card->setStyleSheet(StatisticsStyle::getMetricCardStyle(color));
    card->setFixedHeight(110);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    auto *lblTitle = new QLabel(emoji + " " + title);
    lblTitle->setStyleSheet(StatisticsStyle::MetricTitle);
    *outLabel = new QLabel("Waiting...");
    (*outLabel)->setStyleSheet(StatisticsStyle::getMetricValue());
    (*outLabel)->setTextFormat(Qt::RichText);
    layout->addWidget(lblTitle);
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
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto *lblTitle = new QLabel(title);
    lblTitle->setStyleSheet(StatisticsStyle::SectionTitle);
    headerLayout->addWidget(lblTitle);
    headerLayout->addStretch();
    if (rightWidget) headerLayout->addWidget(rightWidget);
    layout->addWidget(headerWidget);
    *outLayout = layout;
    return frame;
}

static QWidget* createLegendItem(const QString& text, const QString& color) {
    auto* w = new QWidget();
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(6);
    auto* box = new QFrame();
    box->setFixedSize(12, 3);
    box->setStyleSheet(StatisticsStyle::LegendBox.arg(color));
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet(StatisticsStyle::getLegendLabel());
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
    contentWidget->setStyleSheet("");
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
    chartCanvas = new QWidget();
    chartCanvas->setFixedHeight(360);
    chartCanvas->setStyleSheet("background: transparent;");
    chartCanvas->installEventFilter(this);
    chartLayout->addWidget(chartCanvas);
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
        name->setStyleSheet(StatisticsStyle::getProtoLabel());
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

        bar->setStyleSheet(StatisticsStyle::getProtoBarStyle(col));

        auto *percent = new QLabel("0%");
        percent->setFixedWidth(40);
        percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        percent->setStyleSheet(StatisticsStyle::getProtoPercent());
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
    lblEmpty->setStyleSheet(StatisticsStyle::getEmptyLabel());
    topHostsLayout->addWidget(lblEmpty);

    for (int i = 0; i < 5; ++i) {
        auto *r = new QWidget();
        r->setObjectName(QString("HostRow_%1").arg(i));
        auto *rl = new QHBoxLayout(r);
        rl->setContentsMargins(0,0,0,0);
        rl->setSpacing(10);

        auto *ip = new QLabel("N/A");
        ip->setObjectName("lblIp");
        ip->setStyleSheet(StatisticsStyle::getHostIp());
        ip->setFixedWidth(130);

        auto *b = new QProgressBar();
        b->setObjectName("progBar");
        b->setFixedHeight(6);
        b->setTextVisible(false);
        b->setRange(0, 100);
        b->setValue(0);
        b->setStyleSheet(StatisticsStyle::HostBar);

        auto *n = new QLabel("0 B");
        n->setObjectName("lblBytes");
        n->setStyleSheet(StatisticsStyle::getHostNum());
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
        n->setStyleSheet(StatisticsStyle::getLegendLabel());
        *outVal = new QLabel("0 Events");
        (*outVal)->setStyleSheet(StatisticsStyle::getLegendLabel());

        h->addWidget(n);
        h->addStretch();
        h->addWidget(*outVal);
        *outBar = new QProgressBar();
        (*outBar)->setFixedHeight(8);
        (*outBar)->setTextVisible(false);
        (*outBar)->setStyleSheet(StatisticsStyle::getProtoBarStyle(col));
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
    rxSpeedHistory.resize(MAX_HISTORY, 0.0);
    txSpeedHistory.resize(MAX_HISTORY, 0.0);
}

void StatisticsPage::refreshLocalIps() {
    localIps.clear();
    const QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : list) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && addr != QHostAddress::LocalHost) {
            localIps.insert(addr.toIPv4Address()); // 存整数
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
    sourceIpBytes[packet.srcIp] += packet.totalLen;
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
    Q_UNUSED(isDark);
    if (chartCanvas) chartCanvas->update();
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

    if (rxSpeedHistory.size() >= MAX_HISTORY) {
        rxSpeedHistory.erase(rxSpeedHistory.begin());
        txSpeedHistory.erase(txSpeedHistory.begin());
    }
    rxSpeedHistory.push_back(rxMbps);
    txSpeedHistory.push_back(txMbps);

    lblSpeedVal->setText(StatisticsStyle::formatSpeedHtml(formatBytes(smoothRxSpeed), formatBytes(smoothTxSpeed)));
    lblTotalVal->setText(StatisticsStyle::formatTotalHtml(formatBytes((double)totalRxBytes), formatBytes((double)totalTxBytes)));

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
            it.value().bar->setValue(p); it.value().percent->setText(QString::number(p)+"%");
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

    uint64_t maxV = sorted.isEmpty() ? 1 : sorted.first().first;

    QLabel* lblEmptyPtr = topHostsContainer->findChild<QLabel*>("lblEmpty");
    if (lblEmptyPtr) lblEmptyPtr->setVisible(sorted.isEmpty());

    for (int i = 0; i < 5; ++i) {
        QWidget* row = topHostsContainer->findChild<QWidget*>(QString("HostRow_%1").arg(i));
        if (!row) continue;

        if (i < sorted.size()) {
            const auto& p = sorted[i];
            row->findChild<QLabel*>("lblIp")->setText(QHostAddress(p.second).toString());
            row->findChild<QProgressBar*>("progBar")->setValue((int)((double)p.first / maxV * 100));
            row->findChild<QLabel*>("lblBytes")->setText(formatBytes((double)p.first));
            row->setVisible(true);
        } else {
            row->setVisible(false);
        }
    }
    chartCanvas->update();
}

bool StatisticsPage::eventFilter(QObject *w, QEvent *e) {
    if (w == chartCanvas && e->type() == QEvent::Paint) {
        QPainter p(chartCanvas);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = chartCanvas->rect().adjusted(20, 20, -20, -30);

        p.setBrush(palette().color(QPalette::Base));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, 4, 4);

        if(rxSpeedHistory.empty()) return true;
        auto maxR = *std::max_element(rxSpeedHistory.begin(), rxSpeedHistory.end());
        auto maxT = *std::max_element(txSpeedHistory.begin(), txSpeedHistory.end());
        double maxV = std::max(maxR, maxT);
        if(maxV < 0.1) {
            maxV = 0.1; maxV *= 1.2;
        }

        p.setFont(QFont("JetBrains Mono", 14));

        p.setPen(palette().color(QPalette::Text));

        for(int i=0; i<=4; ++i) {
            int y = r.bottom() - r.height()*i/4;
            p.setPen(QPen(palette().color(QPalette::Mid), 1));
            p.drawLine(r.left(), y, r.right(), y);

            p.setPen(palette().color(QPalette::Text));
            p.drawText(QRect(r.right()+8, y-10, 80, 20), Qt::AlignLeft|Qt::AlignVCenter, QString::number(maxV*i/4, 'f', 1) + " Mbps");
        }

        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRect(r.left(), r.bottom()+5, 50, 20), Qt::AlignLeft, "-60s");
        p.drawText(QRect(r.right()-50, r.bottom()+5, 50, 20), Qt::AlignRight, "Now");

        auto draw = [&](const std::vector<double>& d, QColor c) {
            QPainterPath path;
            double sx = (double)r.width()/(MAX_HISTORY-1);
            path.moveTo(r.left(), r.bottom() - (d[0]/maxV)*r.height());
            for(size_t i=1; i<d.size(); ++i) {
                double x = r.left() + i*sx;
                double y = r.bottom() - (d[i]/maxV)*r.height();
                double px = r.left() + (i-1)*sx;
                double py = r.bottom() - (d[i-1]/maxV)*r.height();
                path.cubicTo((px+x)/2, py, (px+x)/2, y, x, y);
            }
            QPainterPath fill = path;
            fill.lineTo(r.right(), r.bottom());
            fill.lineTo(r.left(), r.bottom());
            QLinearGradient g(r.topLeft(), r.bottomLeft());
            g.setColorAt(0, QColor(c.red(), c.green(), c.blue(), 80));
            g.setColorAt(1, Qt::transparent);
            p.setBrush(g);
            p.setPen(Qt::NoPen);
            p.drawPath(fill);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, 2));
            p.drawPath(path);
        };

        draw(rxSpeedHistory, QColor(StatisticsStyle::ColorRate));
        draw(txSpeedHistory, QColor(StatisticsStyle::ColorPPS));
        return true;
    }
    return QWidget::eventFilter(w, e);
}