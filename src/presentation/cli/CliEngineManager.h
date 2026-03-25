#pragma once
#include "presentation/adapters/PipelineAdapter.h"
#include "capture/interface/ICaptureDriver.h"
#include "engine/pipeline/PacketPipeline.h"
#include "common/types/NetworkTypes.h"
#include <QObject>
#include <QVector>
#include <QSharedPointer>
#include <vector>
#include <deque>
#include <string>
#include <atomic>
#include <mutex>
#include <memory>

#ifdef HAVE_CURSES
#include <ncurses.h>
#endif

class CliEngineManager : public QObject {
    Q_OBJECT
public:
    CliEngineManager();
    ~CliEngineManager();
    void start();

private slots:
    void onThreatDetected(const Alert& alert, const ParsedPacket& packet);
    void renderTui();

private:
    void handlePackets(const QSharedPointer<QVector<ParsedPacket>>& packets);

    int workerCount = 1;
    std::string currentDevice;

    uint64_t totalPackets = 0;
    uint64_t totalAlerts = 0;

    std::atomic<uint64_t> alertsCritical{0};
    std::atomic<uint64_t> alertsHigh{0};
    std::atomic<uint64_t> alertsMedium{0};
    std::atomic<uint64_t> alertsLow{0};
    std::atomic<uint64_t> alertsInfo{0};

    std::deque<std::string> latestAlerts;
    std::mutex latestAlertsMutex;

    std::string rulesSource = "builtin";
    size_t loadedRuleCount = 0;

    bool pcapRunning = false;
    bool dbOk = false;
    bool showDetail = false;

    std::string inputBuffer;

#ifdef HAVE_CURSES
    WINDOW* mainWin = nullptr;
    WINDOW* headerWin = nullptr;
    WINDOW* modulesWin = nullptr;
    WINDOW* trafficWin = nullptr;
    WINDOW* alertsWin = nullptr;
    WINDOW* footerWin = nullptr;
#endif

    std::vector<std::unique_ptr<sentinel::engine::PacketPipeline>> pipelinePool;
    std::vector<std::unique_ptr<sentinel::common::SPSCQueue<RawPacket>>> workerQueues;
    std::vector<sentinel::presentation::PipelineAdapter*> adapterPool;
};