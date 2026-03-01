#pragma once
#include "common/types/NetworkTypes.h"
#include "engine/pipeline/PacketPipeline.h"
#include "capture/interface/ICaptureDriver.h"
#include <QObject>
#include <QList>
#include <vector>
#include <deque>
#include <string>

class CliEngineManager : public QObject {
    Q_OBJECT
public:
    CliEngineManager();
    ~CliEngineManager();
    void start();

private slots:
    void onPacketsProcessed(QSharedPointer<QVector<ParsedPacket>> packets);
    void onThreatDetected(const Alert& alert, const ParsedPacket& packet);
    void renderTui();

private:
    int workerCount = 1;
    std::string currentDevice;
    uint64_t totalPackets = 0;
    uint64_t totalAlerts = 0;
    std::deque<std::string> latestAlerts;

    QList<PacketPipeline*> pipelinePool;
    std::vector<sentinel::capture::PacketQueue*> workerQueues;
};