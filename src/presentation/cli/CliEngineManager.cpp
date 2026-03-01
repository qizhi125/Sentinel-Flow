#include "CliEngineManager.h"
#include "capture/impl/PcapCapture.h"
#include "engine/flow/SecurityEngine.h"
#include "engine/context/DatabaseManager.h"
#include <QTimer>
#include <QDateTime>
#include <iostream>
#include <thread>

CliEngineManager::CliEngineManager() {
    int idealThreads = std::thread::hardware_concurrency();
    workerCount = (idealThreads > 4) ? 4 : (idealThreads > 2 ? idealThreads - 2 : 1);
}

CliEngineManager::~CliEngineManager() {
    PcapCapture::instance().stop();
    for (auto* pipe : pipelinePool) { pipe->stopPipeline(); }
    for (auto* pipe : pipelinePool) { pipe->wait(500); delete pipe; }
    for (auto* q : workerQueues) { delete q; }
    DatabaseManager::instance().shutdown();
}

void CliEngineManager::start() {
    std::cout << "\n\033[1;36m[CLI] 初始化底层安全引擎...\033[0m\n";
    SecurityEngine::instance().compileRules();
    DatabaseManager::instance().init("sentinel_data.db");

    for (int i = 0; i < workerCount; ++i) {
        auto* q = new sentinel::capture::PacketQueue();
        workerQueues.push_back(q);
    }

    PcapCapture::instance().init(workerQueues);

    for (int i = 0; i < workerCount; ++i) {
        auto* pipe = new PacketPipeline();
        pipe->setInputQueue(workerQueues[i]);
        pipe->setInspector(&SecurityEngine::instance());
        pipe->setCoreId((i + 1) % std::thread::hardware_concurrency());

        connect(pipe, &PacketPipeline::packetsProcessed, this, &CliEngineManager::onPacketsProcessed, Qt::QueuedConnection);
        connect(pipe, &PacketPipeline::threatDetected, this, &CliEngineManager::onThreatDetected, Qt::QueuedConnection);

        pipelinePool.append(pipe);
        pipe->startPipeline();
    }

    auto devs = PcapCapture::instance().getDeviceList();
    if (!devs.empty()) {
        currentDevice = devs[0];
        PcapCapture::instance().start(currentDevice);
    } else {
        currentDevice = "NONE (离线)";
    }

    QTimer *renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, &CliEngineManager::renderTui);
    renderTimer->start(1000);
}

void CliEngineManager::onPacketsProcessed(QSharedPointer<QVector<ParsedPacket>> packets) {
    totalPackets += packets->size();
}

void CliEngineManager::onThreatDetected(const Alert& alert, const ParsedPacket& packet) {
    totalAlerts++;
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString sevStr, color;
    switch (alert.level) {
        case Alert::Critical: sevStr = "CRITICAL"; color = "\033[1;31m"; break;
        case Alert::High:     sevStr = "HIGH";     color = "\033[1;33m"; break;
        case Alert::Medium:   sevStr = "MEDIUM";   color = "\033[1;34m"; break;
        case Alert::Low:      sevStr = "LOW";      color = "\033[1;37m"; break;
        default:              sevStr = "INFO";     color = "\033[1;32m"; break;
    }
    QString logLine = QString("%1[%2] [%3] %4 -> %5\033[0m")
                          .arg(color, timeStr, sevStr, packet.getSrcStr(), QString::fromStdString(alert.ruleName));
    latestAlerts.push_back(logLine.toStdString());
    if (latestAlerts.size() > 5) latestAlerts.pop_front();
}

void CliEngineManager::renderTui() {
    std::cout << "\033[2J\033[H"; 
    std::cout << "\033[1;36m======================================================================\n";
    std::cout << " Sentinel-Flow v1.0 [CLI Mode] - 高性能网络安全引擎 (Headless)\n";
    std::cout << "======================================================================\033[0m\n";
    std::cout << "\033[1;37m[Engine]\033[0m Workers: " << workerCount 
              << "  |  \033[1;32mStatus: RUNNING\033[0m  |  Interface: \033[1;33m" << currentDevice << "\033[0m\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "\033[1;37m[Traffic]\033[0m Packets Parsed: \033[1;32m" << totalPackets 
              << "\033[0m  |  Threats Blocked: \033[1;31m" << totalAlerts << "\033[0m\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "\033[1;31m[ 实时威胁告警流 (Latest Alerts) ]\033[0m\n";
    
    if (latestAlerts.empty()) {
        std::cout << "  \033[1;32m✓ 当前网络风平浪静，未检测到威胁。\033[0m\n";
    } else {
        for (const auto& alert : latestAlerts) { std::cout << "  " << alert << "\n"; }
    }
    std::cout << "======================================================================\n";
    std::cout << " 按下 \033[1;33mCtrl+C\033[0m 即可安全关闭引擎并退出...\n" << std::flush;
}