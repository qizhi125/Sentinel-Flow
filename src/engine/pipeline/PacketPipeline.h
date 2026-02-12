#pragma once
#include <QThread>
#include <QObject>
#include <QVector>
#include <QMutex>
#include <QWaitCondition>
#include "common/types/NetworkTypes.h"
#include "common/queues/ThreadSafeQueue.h"

#define UI_BATCH_SIZE 2000
#define UI_REFRESH_INTERVAL_MS 200

class PacketPipeline : public QThread {
    Q_OBJECT

public:
    explicit PacketPipeline(QObject *parent = nullptr);
    ~PacketPipeline() override;

    void setInputQueue(ThreadSafeQueue<RawPacket>* queue);

    void startPipeline();
    void stopPipeline();

    signals:
        void packetsProcessed(const QVector<ParsedPacket>& packets);

    void threatDetected(const Alert& alert, const ParsedPacket& packet);

    void statsUpdated(uint64_t bytesProcessed);

protected:
    void run() override;

private:
    ThreadSafeQueue<RawPacket>* inputQueue = nullptr;
    std::atomic<bool> running{false};

    QVector<ParsedPacket> packetBatch;
};