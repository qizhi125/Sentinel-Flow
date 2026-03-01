#include "presentation/cli/CliEngineManager.h"
#include "engine/context/DatabaseManager.h"
#include "engine/flow/SecurityEngine.h"
#include "capture/impl/PcapCapture.h"
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/select.h>
#include <sstream>
#include <fstream>

#ifdef HAVE_CURSES
static constexpr int HEADER_HEIGHT = 3;
static constexpr int MODULES_HEIGHT = 3;
static constexpr int FOOTER_HEIGHT = 2;
#endif

inline void ansi_clear() {
    std::cout << "\033[2J\033[H";
}

inline void ansi_header() {
    std::cout << "======================================================================\n";
    std::cout << " Sentinel-Flow v1.0 [CLI Mode] - 高性能网络安全引擎\n";
    std::cout << "======================================================================\n";
}

CliEngineManager::CliEngineManager() {
    int idealThreads = std::thread::hardware_concurrency();
    workerCount = (idealThreads > 4) ? 4 : (idealThreads > 2 ? idealThreads - 2 : 1);
}

CliEngineManager::~CliEngineManager() {
#ifdef HAVE_CURSES
    if (headerWin) delwin(headerWin);
    if (modulesWin) delwin(modulesWin);
    if (alertsWin) delwin(alertsWin);
    if (footerWin) delwin(footerWin);
    endwin();
#else
    std::cout << "\033[?25h" << std::flush;
#endif

    PcapCapture::instance().stop();

    for (auto& pipe : pipelinePool) {
        pipe->stopPipeline();
    }
    for (auto& pipe : pipelinePool) {
        pipe->wait();
    }

    for (auto* adapter : adapterPool) {
        delete adapter;
    }
    adapterPool.clear();

    pipelinePool.clear();
    workerQueues.clear();

    DatabaseManager::instance().shutdown();
}

void CliEngineManager::start() {
    SecurityEngine::instance().compileRules();
    loadedRuleCount = SecurityEngine::instance().getRules().size();

    DatabaseManager::instance().init("sentinel_data.db");
    dbOk = true;

    std::vector<sentinel::capture::PacketQueue*> rawQueues;
    for (int i = 0; i < workerCount; i++) {
        workerQueues.push_back(std::make_unique<sentinel::common::SPSCQueue<RawPacket>>());
        rawQueues.push_back(workerQueues.back().get());
    }

    PcapCapture::instance().init(rawQueues);

    int idealThreads = std::thread::hardware_concurrency();

    for (int i = 0; i < workerCount; i++) {
        auto pipe = std::make_unique<sentinel::engine::PacketPipeline>();
        pipe->setInputQueue(workerQueues[i].get());
        pipe->setInspector(&SecurityEngine::instance());
        pipe->setCoreId((i + 1) % idealThreads);

        auto* adapter = new sentinel::presentation::PipelineAdapter(this);
        adapter->bindPipeline(pipe.get());

        // 使用 Lambda 将适配器的信号桥接到 CliManager 的函数上
        connect(adapter, &sentinel::presentation::PipelineAdapter::packetsProcessed,
                this, [this](const QSharedPointer<QVector<ParsedPacket>>& packets) { this->handlePackets(packets); }, Qt::QueuedConnection);
        connect(adapter, &sentinel::presentation::PipelineAdapter::threatDetected,
                this, &CliEngineManager::onThreatDetected, Qt::QueuedConnection);

        adapterPool.push_back(adapter);
        pipe->startPipeline();
        pipelinePool.push_back(std::move(pipe));
    }

    auto devs = PcapCapture::instance().getDeviceList();
    if (!devs.empty()) {
        currentDevice = devs[0];
        QStringList args = QCoreApplication::arguments();
        int idx = args.indexOf("--interface");
        if (idx != -1 && idx + 1 < args.size()) {
            currentDevice = args[idx + 1].toStdString();
        }
        PcapCapture::instance().start(currentDevice);
        pcapRunning = true;
    }

#ifdef HAVE_CURSES
    // 初始化 NCURSES 界面 (代码保持原样...)
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
#else
    std::cout << "\033[2J\033[H";
#endif

    QTimer* renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, &CliEngineManager::renderTui);
    renderTimer->start(250);
}

void CliEngineManager::handlePackets(const QSharedPointer<QVector<ParsedPacket>>& packets) {
    totalPackets += packets->size();
}

void CliEngineManager::onThreatDetected(const Alert& alert, const ParsedPacket& packet) {
    Q_UNUSED(packet);
    totalAlerts++;
    switch (alert.level) {
    case Alert::Critical: alertsCritical++; break;
    case Alert::High:     alertsHigh++; break;
    case Alert::Medium:   alertsMedium++; break;
    case Alert::Low:      alertsLow++; break;
    case Alert::Info:     alertsInfo++; break;
    }

    std::lock_guard<std::mutex> lg(latestAlertsMutex);
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    latestAlerts.push_front(QString("[%1] %2").arg(timeStr, QString::fromStdString(alert.description)).toStdString());
    if (latestAlerts.size() > 100) {
        latestAlerts.pop_back();
    }
}

void CliEngineManager::renderTui() {
    ansi_clear();
    ansi_header();
    std::cout << "\033[1;37m[Engine]\033[0m Workers: " << workerCount
              << "  |  \033[1;32mStatus: RUNNING\033[0m  |  Interface: \033[1;33m" << currentDevice << "\033[0m\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "\033[1;37m[Traffic]\033[0m Packets Parsed: \033[1;32m" << totalPackets << "\033[0m";
    std::cout << "  \033[1;37m[Rules]\033[0m Source: " << rulesSource << "  Count: " << loadedRuleCount << "\n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "\033[1;31m[ Alerts Summary ]\033[0m "
              << "\033[1;31mC:\033[0m" << alertsCritical.load() << " "
              << "\033[1;33mH:\033[0m" << alertsHigh.load() << " "
              << "\033[1;34mM:\033[0m" << alertsMedium.load() << " "
              << "\033[1;37mL:\033[0m" << alertsLow.load() << " "
              << "\033[1;32mI:\033[0m" << alertsInfo.load() << " \n";

    std::cout << "----------------------------------------------------------------------\n";
    std::lock_guard<std::mutex> lg(latestAlertsMutex);
    if (latestAlerts.empty()) {
        std::cout << "  \033[1;32m✓ 当前网络风平浪静，未检测到威胁。\033[0m\n";
    } else {
        size_t toShow = showDetail ? std::min<size_t>(latestAlerts.size(), 10) : std::min<size_t>(latestAlerts.size(), 5);
        for (size_t i = 0; i < toShow && i < latestAlerts.size(); ++i) std::cout << "  " << latestAlerts[i] << "\n";
        if (latestAlerts.size() > toShow) std::cout << "  ... (" << latestAlerts.size()-toShow << " more)\n";
    }

    std::cout << "======================================================================\n";
    std::cout << " Commands: r=reload rules  i <path>=import rules  c=clear alerts  d=toggle detail  q=quit\n";
    std::cout << " Enter commands then press Enter.\n" << std::flush;

    fd_set set;
    struct timeval tv = {0, 0};
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    int rv = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);

    if (rv > 0 && FD_ISSET(STDIN_FILENO, &set)) {
        std::string line;
        if (std::getline(std::cin, line)) {
            std::istringstream iss(line);
            std::string cmd; iss >> cmd;
            if (cmd == "q") {
                for (auto& pipe : pipelinePool) pipe->stopPipeline();
                PcapCapture::instance().stop();
                QCoreApplication::quit();
                return;
            } else if (cmd == "r") {
                SecurityEngine::instance().compileRules();
                loadedRuleCount = SecurityEngine::instance().getRules().size();
                std::lock_guard<std::mutex> lg(latestAlertsMutex);
                latestAlerts.push_front("[SYSTEM] Rules reloaded.");
                if (latestAlerts.size() > 50) latestAlerts.pop_back();
            } else if (cmd == "c") {
                std::lock_guard<std::mutex> lg(latestAlertsMutex);
                latestAlerts.clear();
                alertsCritical = alertsHigh = alertsMedium = alertsLow = alertsInfo = 0;
            } else if (cmd == "d") {
                showDetail = !showDetail;
            } else if (cmd == "i") {
                std::string path; if (iss >> path) {
                    std::ifstream ifs(path);
                    if (!ifs.is_open()) {
                        latestAlerts.push_front(std::string("[SYSTEM] Failed to open: ") + path);
                        if (latestAlerts.size() > 50) latestAlerts.pop_back();
                    } else {
                        int maxId = 0; auto existing = SecurityEngine::instance().getRules(); for (const auto &r : existing) if (r.id > maxId) maxId = r.id;
                        int added=0; std::string lineRule;
                        while (std::getline(ifs, lineRule)) {
                            auto start = lineRule.find_first_not_of(" \t\r\n"); if (start==std::string::npos) continue; if (lineRule[start]=='#') continue; auto end = lineRule.find_last_not_of(" \t\r\n"); std::string pattern = lineRule.substr(start, end - start + 1); if (pattern.empty()) continue;
                            IdsRule rule{}; rule.id = ++maxId; rule.enabled=true; rule.protocol = "ANY"; rule.pattern = pattern; rule.level = Alert::Medium; rule.description = "imported"; SecurityEngine::instance().addRule(rule); ++added;
                        }
                        SecurityEngine::instance().compileRules(); loadedRuleCount = SecurityEngine::instance().getRules().size(); latestAlerts.push_front(std::string("[SYSTEM] Imported ") + std::to_string(added) + " rules from: " + path); if (latestAlerts.size()>50) latestAlerts.pop_back();
                    }
                } else {
                    latestAlerts.push_front("[SYSTEM] Import requires a file path (e.g. i /path/to/rules)"); if (latestAlerts.size()>50) latestAlerts.pop_back();
                }
            }
        }
    }
}
