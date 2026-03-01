#include "PacketPipeline.h"
#include "engine/flow/PacketParser.h"
#include "engine/context/DatabaseManager.h"
#include "engine/flow/SecurityEngine.h"
#include <QElapsedTimer>
#include <iostream>
#include <pthread.h>

constexpr int UI_REFRESH_INTERVAL_MS = 150;
constexpr size_t BATCH_RESERVE_SIZE = 5000;

PacketPipeline::PacketPipeline(QObject *parent) : QThread(parent) {
    packetBatch.reserve(BATCH_RESERVE_SIZE);
}

PacketPipeline::~PacketPipeline() {
    stopPipeline();
    if (isRunning()) {
        wait();
    }
}

void PacketPipeline::setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue) {
    inputQueue = queue;
}

void PacketPipeline::setInspector(sentinel::engine::IInspector* inspector) {
    m_inspector = inspector;
}

void PacketPipeline::setCoreId(int coreId) {
    m_coreId = coreId;
}

void PacketPipeline::startPipeline() {
    if (!running) {
        running = true;
        start(QThread::HighPriority);
    }
}

void PacketPipeline::stopPipeline() {
    running = false;
}

void PacketPipeline::run() {
    if (!inputQueue) return;

    if (m_coreId >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(m_coreId, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    uint64_t bytesAccumulator = 0;
    QElapsedTimer timer;
    timer.start();

    while (running) {
        try {
            auto rawOpt = inputQueue->popWait(std::chrono::milliseconds(100));

            if (timer.elapsed() > UI_REFRESH_INTERVAL_MS || packetBatch.size() >= static_cast<qsizetype>(BATCH_RESERVE_SIZE)) {
                if (!packetBatch.isEmpty()) {
                    auto shared = QSharedPointer<QVector<ParsedPacket>>::create(std::move(packetBatch));
                    emit packetsProcessed(shared);
                    packetBatch.clear();
                    packetBatch.reserve(BATCH_RESERVE_SIZE);
                }
                if (bytesAccumulator > 0) {
                    emit statsUpdated(bytesAccumulator);
                    bytesAccumulator = 0;
                }
                timer.restart();
            }

            if (!rawOpt) continue;

            RawPacket& raw = *rawOpt;
            bytesAccumulator += raw.block ? raw.block->size : 0;

            auto parsedOpt = PacketParser::parse(raw);
            if (!parsedOpt) continue;

            ParsedPacket& parsed = *parsedOpt;

            if (SecurityEngine::instance().isIpBlocked(parsed.srcIp) ||
                SecurityEngine::instance().isIpBlocked(parsed.dstIp)) {
                continue;
            }

            if (m_inspector) {
                auto alertOpt = m_inspector->inspect(parsed);
                if (alertOpt) {
                    DatabaseManager::instance().saveAlert(*alertOpt);
                    emit threatDetected(*alertOpt, parsed);
                }
            }

            parsed.block.reset();

            packetBatch.append(std::move(parsed));

        } catch (const std::exception& e) {
            std::cerr << "[!] Pipeline Critical Error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[!] Unknown Pipeline Error" << std::endl;
        }
    }
}