#pragma once
#include <QThread>
#include <QObject>
#include <QVector>
#include <QMutex>
#include <QWaitCondition>
#include <QSharedPointer>
#include "common/types/NetworkTypes.h"
#include "common/queues/SPSCQueue.h"
#include "engine/interface/IInspector.h"

class PacketPipeline : public QThread {
    Q_OBJECT

public:
    explicit PacketPipeline(QObject *parent = nullptr);
    ~PacketPipeline() override;

    void setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue);
    void setInspector(sentinel::engine::IInspector* inspector);
    void setCoreId(int coreId);
    void startPipeline();
    void stopPipeline();

signals:
    void packetsProcessed(QSharedPointer<QVector<ParsedPacket>> packets);
    void threatDetected(const Alert& alert, const ParsedPacket& packet);
    void statsUpdated(uint64_t bytesProcessed);

protected:
    void run() override;

private:
    sentinel::common::SPSCQueue<RawPacket>* inputQueue = nullptr;
    sentinel::engine::IInspector* m_inspector = nullptr;
    std::atomic<bool> running{false};

    int m_coreId = -1;
    QVector<ParsedPacket> packetBatch;
};