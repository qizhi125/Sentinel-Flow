#include "presentation/views/pages/DashboardPage.h"
#include "presentation/views/styles/DashboardStyle.h"
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
#include <QCryptographicHash>
#include <QtMath>

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent) {
    setupUi();
    AuditLogger::instance().addCallback([this](const std::string& msg, const std::string& type){
        QMetaObject::invokeMethod(this, "addSystemLog", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(msg)),
                                  Q_ARG(QString, QString::fromStdString(type)));
    });

    monitorTimer = new QTimer(this);
    connect(monitorTimer, &QTimer::timeout, this, &DashboardPage::updateSystemMetrics);
    monitorTimer->start(2000);
}

RadarWidget::RadarWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scanTimer = new QTimer(this);
    connect(scanTimer, &QTimer::timeout, [this]() {
        scanAngle = (scanAngle + 5) % 360;
        for (auto it = blips.begin(); it != blips.end(); ) {
            it->opacity -= 0.04f;
            if (it->opacity <= 0) it = blips.erase(it); else ++it;
        }
        update();
    });
    scanTimer->start(100);
}

void RadarWidget::addBlip(const QString& sourceIp, const QString& severity) {
    QByteArray hash = QCryptographicHash::hash(sourceIp.toUtf8(), QCryptographicHash::Md5);
    unsigned char firstByte = hash.at(0);
    int angle = (firstByte * 360) / 255;
    QColor c = QColor(DashboardStyle::ColorSafe);
    if (severity == "CRITICAL") c = QColor(DashboardStyle::ColorDanger);
    else if (severity == "HIGH") c = QColor(DashboardStyle::ColorWarning);
    else if (severity == "MEDIUM") c = QColor(DashboardStyle::ColorNeutral);
    blips.push_back({angle, 1.0f, c});
}

void RadarWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w = width(); int h = height();
    QPoint center(w / 2, h / 2);
    int radius = (qMin(w, h) / 2) - 20;

    QColor gridColor = palette().color(QPalette::Mid);
    p.setPen(QPen(gridColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, radius, radius);
    p.drawEllipse(center, radius * 2 / 3, radius * 2 / 3);
    p.drawEllipse(center, radius / 3, radius / 3);
    p.setPen(QPen(gridColor, 1, Qt::DashLine));
    p.drawLine(center.x() - radius, center.y(), center.x() + radius, center.y());
    p.drawLine(center.x(), center.y() - radius, center.x(), center.y() + radius);

    QConicalGradient gradient(center, -scanAngle);
    gradient.setColorAt(0, QColor(0, 191, 165, 100));
    gradient.setColorAt(0.2, Qt::transparent);
    p.setPen(Qt::NoPen); p.setBrush(gradient);
    p.drawPie(center.x() - radius, center.y() - radius, radius * 2, radius * 2, -scanAngle * 16, 70 * 16);

    for (const auto& blip : blips) {
        double rad = qDegreesToRadians((double)(blip.angle - 90));
        int x = center.x() + (int)(radius * 0.8 * cos(rad));
        int y = center.y() + (int)(radius * 0.8 * sin(rad));
        QColor c = blip.color; c.setAlphaF(blip.opacity);
        p.setBrush(c); p.setPen(Qt::NoPen);
        p.drawEllipse(QPoint(x, y), 8, 8);
    }
}

void DashboardPage::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 20, 30, 20);
    mainLayout->setSpacing(20);

    auto *cardsLayout = new QHBoxLayout(); cardsLayout->setSpacing(20);
    auto createCard = [](const QString &t, const QString &v, const QString &col) {
        auto *card = new QFrame(); card->setObjectName("Card");
        card->setStyleSheet(DashboardStyle::getCardStyle(col));
        auto *l = new QVBoxLayout(card); l->setContentsMargins(20, 15, 20, 15);
        auto *tl = new QLabel(t); tl->setStyleSheet(DashboardStyle::getStatusTitle());
        auto *vl = new QLabel(v); vl->setObjectName("val"); vl->setStyleSheet(DashboardStyle::getStatusValue());
        l->addWidget(tl); l->addWidget(vl); l->addStretch(); return card;
    };

    auto *c1 = createCard("系统安全评分", "100", DashboardStyle::ColorSafe);
    lblSafetyScore = c1->findChild<QLabel*>("val");
    auto *c2 = createCard("当前威胁等级", "安全", DashboardStyle::ColorSafe);
    lblThreatLevel = c2->findChild<QLabel*>("val");
    auto *c3 = createCard("防护模块状态", "初始化", DashboardStyle::ColorSafe);
    lblActiveProtections = c3->findChild<QLabel*>("val");

    cardsLayout->addWidget(c1); cardsLayout->addWidget(c2); cardsLayout->addWidget(c3);
    mainLayout->addLayout(cardsLayout);

    auto *midLayout = new QHBoxLayout(); midLayout->setSpacing(20);
    auto *radarBox = new QFrame(); radarBox->setObjectName("Card");
    auto *radarL = new QVBoxLayout(radarBox);
    auto *rl = new QLabel("实时威胁感知");
    rl->setStyleSheet(DashboardStyle::getSectionTitle());
    radarWidget = new RadarWidget();
    radarL->addWidget(rl);
    radarL->addWidget(radarWidget, 1, Qt::AlignCenter);
    midLayout->addWidget(radarBox, 4);

    auto *logBox = new QFrame(); logBox->setObjectName("Card");
    auto *logL = new QVBoxLayout(logBox);
    auto *ll = new QLabel("系统实时审计");
    ll->setStyleSheet(DashboardStyle::getSectionTitle());
    activityList = new QListWidget();
    activityList->setStyleSheet(DashboardStyle::getActivityList());
    logL->addWidget(ll);
    logL->addWidget(activityList);
    midLayout->addWidget(logBox, 6);
    mainLayout->addLayout(midLayout, 2);

    auto *botLayout = new QHBoxLayout();
    botLayout->setSpacing(20);
    auto *svcBox = new QFrame();
    svcBox->setObjectName("Card");
    auto *svcL = new QVBoxLayout(svcBox);
    auto *sl = new QLabel("核心服务状态");
    sl->setStyleSheet(DashboardStyle::getSectionTitle());
    svcL->addWidget(sl);
    auto createSvc = [](const QString& n, QLabel** l) {
        auto *w = new QWidget();
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0,0,0,0);
        auto *name = new QLabel(n);
        name->setStyleSheet(DashboardStyle::getServiceLabel());
        *l = new QLabel("检测中...");
        (*l)->setStyleSheet(DashboardStyle::getServiceLabel());
        h->addWidget(name);
        h->addStretch();
        h->addWidget(*l); return w;
    };
    svcL->addWidget(createSvc("AI 检测引擎", &lblServiceAI));
    svcL->addWidget(createSvc("数据库管理", &lblServiceDB));
    svcL->addWidget(createSvc("数据包捕获", &lblServiceNet));
    svcL->addStretch();
    botLayout->addWidget(svcBox, 4);

    auto *sysBox = new QFrame();
    sysBox->setObjectName("Card");
    auto *sysL = new QVBoxLayout(sysBox);
    auto *sysTitle = new QLabel("系统资源监控");
    sysTitle->setStyleSheet(DashboardStyle::getSectionTitle());
    sysL->addWidget(sysTitle);

    auto createRes = [](const QString& n, const QString& col, QProgressBar** b, QLabel** v) {
        auto *w = new QWidget(); auto *vL = new QVBoxLayout(w); vL->setContentsMargins(0,5,0,8);
        auto *h = new QHBoxLayout();
        auto *name = new QLabel(n);
        name->setStyleSheet(DashboardStyle::getResourceTitle());
        *v = new QLabel("0%");
        (*v)->setStyleSheet(DashboardStyle::getResourceValue());
        h->addWidget(name); h->addStretch(); h->addWidget(*v);
        *b = new QProgressBar(); (*b)->setFixedHeight(8);
        (*b)->setTextVisible(false);
        (*b)->setStyleSheet(DashboardStyle::getProgressBarStyle(col));
        vL->addLayout(h); vL->addWidget(*b); return w;
    };
    sysL->addWidget(createRes("CPU 使用率", DashboardStyle::ColorDanger, &barCpu, &lblCpuVal));
    sysL->addWidget(createRes("内存 使用率", DashboardStyle::ColorInfo, &barRam, &lblRamVal));
    sysL->addWidget(createRes("磁盘 使用率", DashboardStyle::ColorWarning, &barDisk, &lblDiskVal));
    sysL->addStretch();
    botLayout->addWidget(sysBox, 6);
    mainLayout->addLayout(botLayout, 1);
}

void DashboardPage::addSystemLog(const QString& message, const QString& type) {
    QString timeStr = QTime::currentTime().toString("HH:mm:ss");
    QString fullMsg = QString("[%1] %2").arg(timeStr, message);

    auto *item = new QListWidgetItem(fullMsg);

    if (type == "ALERT") {
        item->setForeground(QColor(DashboardStyle::ColorDanger));
    } else if (type == "WARN") {
        item->setForeground(QColor(DashboardStyle::ColorWarning));
    } else if (type == "SUCCESS") {
        item->setForeground(QColor(DashboardStyle::ColorSafe));
    } else {
        item->setForeground(palette().color(QPalette::Text));
    }

    activityList->addItem(item);

    activityList->scrollToBottom();

    if (activityList->count() > 50) {
        delete activityList->takeItem(0);
    }
}

void DashboardPage::updateSecurityStatus(int totalAlerts, int activeModules) {
    int score = std::clamp(100 - totalAlerts, 0, 100);
    QString statusColor = DashboardStyle::ColorSafe;
    QString statusText = "安全";
    if (score < 60) {
        statusColor = DashboardStyle::ColorDanger;
        statusText = "严重威胁";
    } else if (score < 85) {
        statusColor = DashboardStyle::ColorWarning;
        statusText = "风险提升";
    }
    lblSafetyScore->setText(QString::number(score));
    lblSafetyScore->setStyleSheet(DashboardStyle::getStatusValue() + " color: " + statusColor + ";");
    lblThreatLevel->setText(statusText);
    lblThreatLevel->setStyleSheet(DashboardStyle::getStatusValue() + " color: " + statusColor + ";");
    lblActiveProtections->setText(QString("%1 / 5").arg(activeModules));
    lblActiveProtections->setStyleSheet(DashboardStyle::getStatusValue() + " color: " + DashboardStyle::ColorNeutral + ";");
}

void DashboardPage::updateServiceStatus(bool aiOk, bool dbOk, bool netOk) {
    auto setStatus = [](QLabel* lbl, bool ok) {
        lbl->setText(ok ? "运行中" : "异常");
        lbl->setStyleSheet(ok ? DashboardStyle::ServiceStatusOK : DashboardStyle::ServiceStatusERR);
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
    if (parts.size() < 5) return 0.0;
    unsigned long long user = parts[1].toULongLong();
    unsigned long long nice = parts[2].toULongLong();
    unsigned long long system = parts[3].toULongLong();
    unsigned long long idle = parts[4].toULongLong();
    unsigned long long total = user + nice + system + idle;
    double percent = 0.0;
    if (prevTotal > 0 && total > prevTotal) {
        percent = (double)((total - prevTotal) - (idle - prevIdle)) / (total - prevTotal) * 100.0;
    }
    prevTotal = total; prevIdle = idle;
    return std::clamp(percent, 0.0, 100.0);
}

double DashboardPage::getRealRamUsage() {
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0;
    QTextStream in(&file);
    unsigned long long total = 0, avail = 0;
    QString line;
    while (in.readLineInto(&line)) {
        if (line.startsWith("MemTotal:")) total = line.split(" ", Qt::SkipEmptyParts)[1].toULongLong();
        if (line.startsWith("MemAvailable:")) avail = line.split(" ", Qt::SkipEmptyParts)[1].toULongLong();
    }
    file.close();
    return total ? (double)(total - avail) / total * 100.0 : 0.0;
}

double DashboardPage::getDiskUsage() {
    struct statvfs stat{};
    if (statvfs("/", &stat) != 0) return 0.0;
    return (double)(stat.f_blocks - stat.f_bfree) / stat.f_blocks * 100.0;
}

void DashboardPage::triggerRadarAlert(const QString& sourceIp, const QString& severity) {
    radarWidget->addBlip(sourceIp, severity);
}

void DashboardPage::updateSystemMetrics() {
    int cpu = (int)getRealCpuUsage();
    int ram = (int)getRealRamUsage();
    int disk = (int)getDiskUsage();
    barCpu->setValue(cpu); lblCpuVal->setText(QString::number(cpu)+"%");
    barRam->setValue(ram); lblRamVal->setText(QString::number(ram)+"%");
    barDisk->setValue(disk); lblDiskVal->setText(QString::number(disk)+"%");
}

void DashboardPage::setTheme(bool isDark) {
    Q_UNUSED(isDark);
    // 强制雷达图重绘，以便重新获取 palette().color(QPalette::Mid)
    if (radarWidget) {
        radarWidget->update();
    }

    // 如果有其他需要手动切换颜色的 Label 或 Bar，可以在这里处理
    // 由于使用了 stylesheet 中的 palette() 宏，大部分标准控件会自动更新
}