#include "presentation/views/pages/DashboardPage.h"
#include "presentation/views/styles/global.h"
#include "engine/governance/AuditLogger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDateTime>
#include <QPainterPath>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <sys/statvfs.h>
#include <QtMath>
#include <QDebug>
#include <QTime>
#include <QFont>
#include <QIcon>
#include <QScrollBar>
#include <QStyle>

ThreatTimelineWidget::ThreatTimelineWidget(QWidget *parent) : QChartView(parent) {
    setupChart();
}

void ThreatTimelineWidget::setupChart() {
    m_chart = new QChart();
    m_chart->setBackgroundBrush(Qt::NoBrush);
    m_chart->setPlotAreaBackgroundBrush(Qt::NoBrush);
    m_chart->setAnimationOptions(QChart::SeriesAnimations);
    m_chart->setMargins(QMargins(0, 0, 0, 0));

    auto setupScatter = [](QScatterSeries *series, const QString &name, QColor color, int size) {
        series->setName(name);
        series->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        series->setMarkerSize(size);
        series->setPen(QPen(color, 2.0));
        color.setAlpha(40);
        series->setBrush(color);
    };

    m_seriesCritical = new QScatterSeries();
    setupScatter(m_seriesCritical, "严重", QColor("#FF3333"), 16);

    m_seriesHigh = new QScatterSeries();
    setupScatter(m_seriesHigh, "高危", QColor("#F1C40F"), 14);

    m_seriesMedium = new QScatterSeries();
    setupScatter(m_seriesMedium, "中危", QColor(Style::ColorAccent()), 12);

    m_seriesLow = new QScatterSeries();
    setupScatter(m_seriesLow, "低危", QColor("#888888"), 10);

    m_chart->addSeries(m_seriesCritical);
    m_chart->addSeries(m_seriesHigh);
    m_chart->addSeries(m_seriesMedium);
    m_chart->addSeries(m_seriesLow);

    m_axisX = new QDateTimeAxis();
    m_axisX->setFormat("HH:mm:ss");
    m_axisX->setLabelsFont(QFont("JetBrains Mono", 10));
    m_axisX->setLineVisible(false);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QCategoryAxis();
    m_axisY->setLabelsPosition(QCategoryAxis::AxisLabelsPositionCenter);
    m_axisY->setRange(0.5, 4.5);

    m_axisY->append("低\n危", 1.0);
    m_axisY->append("中\n危", 2.0);
    m_axisY->append("高\n危", 3.0);
    m_axisY->append("严\n重", 4.0);

    m_axisY->setLabelsFont(QFont("Inter", 11, QFont::Bold));
    m_axisY->setLineVisible(false);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    for (auto* series : {m_seriesCritical, m_seriesHigh, m_seriesMedium, m_seriesLow}) {
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
    }

    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignTop);
    m_chart->legend()->setFont(QFont("Inter", 10));
    m_chart->legend()->setBackgroundVisible(false);
    m_chart->legend()->setMarkerShape(QLegend::MarkerShapeCircle);

    setChart(m_chart);
    setRenderHint(QPainter::Antialiasing);
    setTheme(g_isDarkMode);
}

void ThreatTimelineWidget::addEvent(const ThreatEvent& event) {
    m_events.push_back(event);
    if (m_events.size() > MAX_EVENTS) m_events.pop_front();

    double y = 1.0;
    QScatterSeries* targetSeries = m_seriesLow;

    if (event.severity == "CRITICAL") { y = 4.0; targetSeries = m_seriesCritical; }
    else if (event.severity == "HIGH") { y = 3.0; targetSeries = m_seriesHigh; }
    else if (event.severity == "MEDIUM") { y = 2.0; targetSeries = m_seriesMedium; }

    targetSeries->append(event.timestamp, y);

    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - TIME_WINDOW_MS;
    for (auto* series : {m_seriesCritical, m_seriesHigh, m_seriesMedium, m_seriesLow}) {
        QList<QPointF> points = series->points();
        series->clear();
        for (const QPointF& p : points) {
            if (p.x() >= cutoff) series->append(p);
        }
    }
    updateAxisRange();
}

void ThreatTimelineWidget::clear() {
    m_events.clear();
    m_seriesCritical->clear(); m_seriesHigh->clear();
    m_seriesMedium->clear(); m_seriesLow->clear();
    updateAxisRange();
}

void ThreatTimelineWidget::updateAxisRange() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(now - TIME_WINDOW_MS), QDateTime::fromMSecsSinceEpoch(now + 1000));
}

void ThreatTimelineWidget::resizeEvent(QResizeEvent *event) { QChartView::resizeEvent(event); }

void ThreatTimelineWidget::setTheme(bool isDark) {
    QColor textColor = isDark ? QColor("#888888") : QColor("#666666");
    QColor gridColor = isDark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 20);

    QPen gridPen(gridColor);
    gridPen.setWidth(1);
    gridPen.setStyle(Qt::DashLine);

    m_axisX->setLabelsColor(textColor); m_axisX->setGridLinePen(gridPen);
    m_axisY->setLabelsColor(textColor); m_axisY->setGridLinePen(gridPen);
    m_chart->legend()->setLabelColor(textColor);

    QColor accent = QColor(Style::ColorAccent());
    m_seriesMedium->setPen(QPen(accent, 2.0));
    accent.setAlpha(40);
    m_seriesMedium->setBrush(accent);
}

DashboardPage::DashboardPage(QWidget *parent) : ThemeablePage(parent) {
    setupUi();
    AuditLogger::instance().addCallback([this](const std::string& msg, const std::string& type){
        QMetaObject::invokeMethod(this, "addSystemLog", Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(msg)), Q_ARG(QString, QString::fromStdString(type)));
    });
    monitorTimer = new QTimer(this);
    connect(monitorTimer, &QTimer::timeout, this, &DashboardPage::updateSystemMetrics);
    monitorTimer->start(1000);
}

void DashboardPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20); mainLayout->setSpacing(16);

    auto *cardsLayout = new QHBoxLayout(); cardsLayout->setSpacing(16);
    auto createCard = [](const QString &t, const QString &v, const QString &status) {
        auto *card = new QFrame();
        card->setObjectName("Card");
        card->setProperty("status", status);
        auto *l = new QVBoxLayout(card); l->setContentsMargins(20, 15, 20, 15);

        auto *tl = new QLabel(t);
        tl->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));

        auto *vl = new QLabel(v); vl->setObjectName("val"); vl->setProperty("role", "value");
        vl->setStyleSheet("background: transparent;");
        l->addWidget(tl); l->addWidget(vl); l->addStretch(); return card;
    };

    auto *c1 = createCard("动态健康评分 (实时)", "100", "primary"); lblSafetyScore = c1->findChild<QLabel*>("val");
    auto *c2 = createCard("当前态势级别 (实时)", "系统安全", "primary"); lblThreatLevel = c2->findChild<QLabel*>("val");
    auto *c3 = createCard("核心防护模块", "0/5", "primary"); lblActiveProtections = c3->findChild<QLabel*>("val");
    cardsLayout->addWidget(c1); cardsLayout->addWidget(c2); cardsLayout->addWidget(c3);
    mainLayout->addLayout(cardsLayout);

    auto *midLayout = new QHBoxLayout(); midLayout->setSpacing(16);
    auto *timelineBox = new QFrame(); timelineBox->setObjectName("Card");
    auto *timelineL = new QVBoxLayout(timelineBox);
    auto *tl = new QLabel("实时威胁时间轴");
    tl->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));
    threatTimeline = new ThreatTimelineWidget(this);
    timelineL->addWidget(tl); timelineL->addWidget(threatTimeline, 1);

    midLayout->addWidget(timelineBox, 5);

    auto *logBox = new QFrame(); logBox->setObjectName("Card");
    auto *logL = new QVBoxLayout(logBox);
    auto *ll = new QLabel("系统操作流审计");
    ll->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));

    activityConsole = new QTextEdit();
    activityConsole->setReadOnly(true);
    activityConsole->setFrameShape(QFrame::NoFrame);
    activityConsole->document()->setMaximumBlockCount(300);
    activityConsole->setStyleSheet("QTextEdit { background: transparent; color: #A0A0A0; font-family: 'Consolas', 'JetBrains Mono', monospace; font-size: 13px; line-height: 1.5; outline: none; }");

    logL->addWidget(ll); logL->addWidget(activityConsole);

    midLayout->addWidget(logBox, 5);

    mainLayout->addLayout(midLayout, 2);

    auto *botLayout = new QHBoxLayout(); botLayout->setSpacing(16);
    auto *svcBox = new QFrame(); svcBox->setObjectName("Card");
    auto *svcL = new QVBoxLayout(svcBox);
    auto *sl = new QLabel("核心服务状态");
    sl->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));
    svcL->addWidget(sl);
    auto createSvc = [](const QString& n, QLabel** l) {
        auto *w = new QWidget(); w->setStyleSheet("background: transparent;");
        auto *h = new QHBoxLayout(w); h->setContentsMargins(0,0,0,0);
        auto *name = new QLabel(n); name->setStyleSheet("font-weight:bold; background: transparent;");
        *l = new QLabel("检测中..."); (*l)->setStyleSheet("font-weight:bold; background: transparent;");
        h->addWidget(name); h->addStretch(); h->addWidget(*l); return w;
    };
    svcL->addWidget(createSvc("AI 检测引擎", &lblServiceAI));
    svcL->addWidget(createSvc("数据库管理", &lblServiceDB));
    svcL->addWidget(createSvc("数据包捕获", &lblServiceNet));
    svcL->addStretch(); botLayout->addWidget(svcBox, 5);

    auto *sysBox = new QFrame(); sysBox->setObjectName("Card");
    auto *sysL = new QVBoxLayout(sysBox);
    auto *sysTitle = new QLabel("系统资源监控");
    sysTitle->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1; background: transparent;").arg(Style::ColorAccent()));
    sysL->addWidget(sysTitle);

    auto createRes = [](const QString& n, const QString& status, QProgressBar** b, QLabel** v) {
        auto *w = new QWidget(); w->setStyleSheet("background: transparent;");
        auto *vL = new QVBoxLayout(w); vL->setContentsMargins(0,5,0,8);
        auto *h = new QHBoxLayout();
        auto *name = new QLabel(n); name->setStyleSheet("font-weight: bold; background: transparent;");
        *v = new QLabel("0%"); (*v)->setStyleSheet("font-weight: bold; background: transparent;");
        h->addWidget(name); h->addStretch(); h->addWidget(*v);
        *b = new QProgressBar(); (*b)->setFixedHeight(8); (*b)->setTextVisible(false);
        (*b)->setProperty("status", status);
        vL->addLayout(h); vL->addWidget(*b); return w;
    };
    sysL->addWidget(createRes("CPU 使用率", "danger", &barCpu, &lblCpuVal));
    sysL->addWidget(createRes("内存 使用率", "info", &barRam, &lblRamVal));
    sysL->addWidget(createRes("磁盘 使用率", "warning", &barDisk, &lblDiskVal));
    sysL->addStretch(); botLayout->addWidget(sysBox, 5);
    mainLayout->addLayout(botLayout, 1);
}

void DashboardPage::addSystemLog(const QString& message, const QString& type) {
    QString timeStr = QTime::currentTime().toString("HH:mm:ss");
    QString color = "#A0A0A0";
    QString prefix = "[INFO]";

    if (type == "ALERT") { color = Style::ColorDanger(); prefix = "[WARN]"; }
    else if (type == "WARN") { color = Style::ColorWarning(); prefix = "[INFO]"; }
    else if (type == "SUCCESS") { color = Style::ColorAccent(); prefix = "[ OK ]"; }

    QString html = QString("<div style='margin-bottom: 2px;'><span style='color: #666;'>%1</span> "
                           "<span style='color: %2;'><b>%3</b> %4</span></div>")
                       .arg(timeStr, color, prefix, message);

    activityConsole->append(html);
    QScrollBar *scrollbar = activityConsole->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void DashboardPage::updateSecurityStatus(int totalAlerts, int activeModules) {
    Q_UNUSED(totalAlerts);
    lblActiveProtections->setText(QString("%1 / 5").arg(activeModules));
}

void DashboardPage::calculateDynamicScore() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    while (!m_slidingWindowAlerts.empty() && (now - m_slidingWindowAlerts.front().timestamp > SCORE_WINDOW_MS)) {
        m_slidingWindowAlerts.pop_front();
    }

    double totalPressure = 0.0;
    int criticalCount = 0;
    int highCount = 0;

    for (const auto& event : m_slidingWindowAlerts) {
        if (event.severity == "CRITICAL") { criticalCount++; totalPressure += 5000.0; }
        else if (event.severity == "HIGH") { highCount++; totalPressure += 50.0; }
        else if (event.severity == "MEDIUM") { totalPressure += 5.0; }
        else { totalPressure += 0.2; }
    }

    double targetScore = 100.0;

    if (totalPressure < 2000) {
        targetScore = 100.0 - (totalPressure / 2000.0) * 10.0;
    } else if (totalPressure < 10000) {
        targetScore = 90.0 - ((totalPressure - 2000) / 8000.0) * 15.0;
    } else if (totalPressure < 50000) {
        targetScore = 75.0 - ((totalPressure - 10000) / 40000.0) * 15.0;
    } else {
        targetScore = 60.0 - ((totalPressure - 50000) / 100000.0) * 60.0;
    }

    if (criticalCount > 0) {
        targetScore = std::min(59.0, targetScore);
    } else {
        targetScore = std::max(60.0, targetScore);

        if (highCount > 20) {
            targetScore = std::min(74.0, targetScore);
        }
    }

    if (m_currentHealthScore < targetScore) {
        m_currentHealthScore += 0.8;
        if (m_currentHealthScore > targetScore) m_currentHealthScore = targetScore;
    } else if (m_currentHealthScore > targetScore) {
        m_currentHealthScore -= 3.0;
        if (m_currentHealthScore < targetScore) m_currentHealthScore = targetScore;
    }

    int displayScore = static_cast<int>(m_currentHealthScore);

    QString statusColor, statusText, cardStatus;
    if (displayScore < 60) {
        statusColor = "#FF3333"; statusText = "严重威胁"; cardStatus = "danger";
    } else if (displayScore < 75) {
        statusColor = "#F39C12"; statusText = "高危洪峰"; cardStatus = "warning";
    } else if (displayScore < 90) {
        statusColor = "#F1C40F"; statusText = "风险预警"; cardStatus = "warning";
    } else {
        statusColor = Style::ColorAccent(); statusText = "系统安全"; cardStatus = "primary";
    }

    auto updateCard = [](QLabel* lbl, const QString& col, const QString& status) {
        lbl->setStyleSheet(QString("color: %1; background: transparent;").arg(col));
        QWidget* card = lbl->parentWidget();
        card->setProperty("status", status);
        card->style()->unpolish(card);
        card->style()->polish(card);
    };

    lblSafetyScore->setText(QString::number(displayScore));
    updateCard(lblSafetyScore, statusColor, cardStatus);
    lblThreatLevel->setText(statusText);
    updateCard(lblThreatLevel, statusColor, cardStatus);
}

void DashboardPage::updateServiceStatus(bool aiOk, bool dbOk, bool netOk) {
    auto setStatus = [](QLabel* lbl, bool ok) {
        lbl->setText(ok ? "运行中" : "异常");
        lbl->setStyleSheet(QString("color: %1; font-weight:bold; background: transparent;")
                           .arg(ok ? Style::ColorAccent() : Style::ColorDanger()));
    };
    setStatus(lblServiceAI, aiOk);
    setStatus(lblServiceDB, dbOk);
    setStatus(lblServiceNet, netOk);
}

double DashboardPage::getRealCpuUsage() {
    QFile file("/proc/stat");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;
    QTextStream in(&file); QString line = in.readLine(); file.close();
    QStringList parts = line.split(" ", Qt::SkipEmptyParts);
    if (parts.size() < 8) return 0.0;

    unsigned long long user = parts[1].toULongLong();
    unsigned long long nice = parts[2].toULongLong();
    unsigned long long system = parts[3].toULongLong();
    unsigned long long idle = parts[4].toULongLong();
    unsigned long long iowait = parts[5].toULongLong();
    unsigned long long irq = parts[6].toULongLong();
    unsigned long long softirq = parts[7].toULongLong();

    unsigned long long currentIdle = idle + iowait;
    unsigned long long currentTotal = user + nice + system + currentIdle + irq + softirq;

    double percent = 0.0;
    if (prevTotal > 0 && currentTotal > prevTotal) {
        unsigned long long totalDiff = currentTotal - prevTotal;
        unsigned long long idleDiff = currentIdle - prevIdle;
        percent = (double)(totalDiff - idleDiff) / totalDiff * 100.0;
    }
    prevTotal = currentTotal; prevIdle = currentIdle;
    return std::clamp(percent, 0.0, 100.0);
}

double DashboardPage::getRealRamUsage() {
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;
    QTextStream in(&file); unsigned long long total = 0, avail = 0; QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith("MemTotal:")) total = line.split(" ", Qt::SkipEmptyParts)[1].toULongLong();
        if (line.startsWith("MemAvailable:")) avail = line.split(" ", Qt::SkipEmptyParts)[1].toULongLong();
    }
    file.close(); return total ? (double)(total - avail) / total * 100.0 : 0.0;
}

double DashboardPage::getDiskUsage() {
    struct statvfs stat{};
    if (statvfs("/", &stat) != 0) return 0.0;
    return (double)(stat.f_blocks - stat.f_bfree) / stat.f_blocks * 100.0;
}

void DashboardPage::triggerRadarAlert(const QString& sourceIp, const QString& severity) {
    Q_UNUSED(sourceIp);
    ThreatEvent event;
    event.timestamp = QDateTime::currentMSecsSinceEpoch();
    event.severity = severity;
    if (threatTimeline) {
        threatTimeline->addEvent(event);
    }
    m_slidingWindowAlerts.push_back(event);
}

void DashboardPage::updateSystemMetrics() {
    int cpu = (int)getRealCpuUsage(); int ram = (int)getRealRamUsage(); int disk = (int)getDiskUsage();
    barCpu->setValue(cpu); lblCpuVal->setText(QString::number(cpu)+"%");
    barRam->setValue(ram); lblRamVal->setText(QString::number(ram)+"%");
    barDisk->setValue(disk); lblDiskVal->setText(QString::number(disk)+"%");

    calculateDynamicScore();
}

void DashboardPage::setTheme(bool isDark) {
    if (threatTimeline) threatTimeline->setTheme(isDark);
}

void DashboardPage::onThemeChanged() {
    setTheme(g_isDarkMode);
}