#include "PipelineAdapter.h"
#include <QMetaObject>

namespace sentinel::presentation {

    PipelineAdapter::PipelineAdapter(QObject* parent) : QObject(parent) {}

    void PipelineAdapter::bindPipeline(sentinel::engine::PacketPipeline* pipeline) {
        if (!pipeline) return;

        // 注入回调：捕获 this 指针，将标准回调桥接到内部处理函数
        pipeline->setCallBack(
            [this](std::shared_ptr<std::vector<ParsedPacket>> batch) { this->onBatchReady(batch); },
            [this](const Alert& alert, const ParsedPacket& packet) { this->onThreatReady(alert, packet); },
            [this](uint64_t bytes) { this->onStatsReady(bytes); }
        );
    }

    void PipelineAdapter::onBatchReady(std::shared_ptr<std::vector<ParsedPacket>> batch) {
        if (!batch || batch->empty()) return;

        // 转换为 Qt 结构，适配现有的 TrafficTableModel
        auto qBatch = QSharedPointer<QVector<ParsedPacket>>::create();
        qBatch->reserve(batch->size());
        for (auto& pkt : *batch) {
            qBatch->append(std::move(pkt));
        }

        // 强制使用 QueuedConnection 跨线程投递至主线程
        QMetaObject::invokeMethod(this, [this, qBatch]() {
            emit packetsProcessed(qBatch);
        }, Qt::QueuedConnection);
    }

    void PipelineAdapter::onThreatReady(const Alert& alert, const ParsedPacket& packet) {
        QMetaObject::invokeMethod(this, [this, alert, packet]() {
            emit threatDetected(alert, packet);
        }, Qt::QueuedConnection);
    }

    void PipelineAdapter::onStatsReady(uint64_t bytes) {
        QMetaObject::invokeMethod(this, [this, bytes]() {
            emit statsUpdated(bytes);
        }, Qt::QueuedConnection);
    }

}