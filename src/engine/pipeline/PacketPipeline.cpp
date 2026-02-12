#include "engine/pipeline/PacketPipeline.h"
#include "engine/flow/PacketParser.h"
#include "engine/flow/SecurityEngine.h"
#include "engine/context/DatabaseManager.h"
#include <QElapsedTimer>
#include <QDebug>
#include <iostream>

#undef UI_BATCH_SIZE
#define UI_BATCH_SIZE 200
#undef UI_REFRESH_INTERVAL_MS
#define UI_REFRESH_INTERVAL_MS 200

PacketPipeline::PacketPipeline(QObject *parent) : QThread(parent) {
    packetBatch.reserve(UI_BATCH_SIZE * 2);
}

PacketPipeline::~PacketPipeline() {
    stopPipeline();
    wait();
}

void PacketPipeline::setInputQueue(ThreadSafeQueue<RawPacket>* queue) {
    inputQueue = queue;
}

void PacketPipeline::startPipeline() {
    if (!running) {
        running = true;
        start();
    }
}

void PacketPipeline::stopPipeline() {
    running = false;
}

void PacketPipeline::run() {
    if (!inputQueue) return;

    QElapsedTimer timer;
    timer.start();
    uint64_t bytesAccumulator = 0;

    while (running) {
        try {
            auto rawOpt = inputQueue->tryPop();

            if (!rawOpt) {
                bool hasPending = !packetBatch.isEmpty();
                bool shouldFlush = hasPending && (timer.elapsed() > 50);

                if (shouldFlush) {
                    emit packetsProcessed(packetBatch);
                    packetBatch.clear(); // 清空，但保留 capacity

                    if (bytesAccumulator > 0) {
                        emit statsUpdated(bytesAccumulator);
                        bytesAccumulator = 0;
                    }
                    timer.restart();
                }
                QThread::msleep(10);
                continue;
            }

            RawPacket raw = *rawOpt;
            uint32_t len = raw.block ? raw.block->size : 0;
            bytesAccumulator += len;

            auto parsedOpt = PacketParser::parse(raw);
            if (!parsedOpt) continue;

            ParsedPacket parsed = *parsedOpt;

            auto alertOpt = SecurityEngine::instance().inspect(parsed);
            if (alertOpt) {
                DatabaseManager::instance().saveAlert(*alertOpt);
                emit threatDetected(*alertOpt, parsed);
            }

            packetBatch.append(parsed);

            bool batchFull = packetBatch.size() >= UI_BATCH_SIZE;
            bool timeUp = timer.elapsed() > UI_REFRESH_INTERVAL_MS;

            if (batchFull || timeUp) {
                if (!packetBatch.isEmpty()) {
                    emit packetsProcessed(packetBatch);
                    packetBatch.clear();
                }

                if (bytesAccumulator > 0) {
                    emit statsUpdated(bytesAccumulator);
                    bytesAccumulator = 0;
                }
                timer.restart();
            }

        } catch (const std::exception& e) {
            std::cerr << "⚠️ Pipeline Exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "⚠️ Unknown Pipeline Error" << std::endl;
        }
    }
}