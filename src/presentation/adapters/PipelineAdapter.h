#pragma once
#include "engine/pipeline/PacketPipeline.h"
#include "common/types/NetworkTypes.h"
#include <QSharedPointer>
#include <QObject>
#include <QVector>

namespace sentinel::presentation {

    class PipelineAdapter : public QObject {
        Q_OBJECT
    public:
        explicit PipelineAdapter(QObject* parent = nullptr);

        // 绑定底层 C++ 管线
        void bindPipeline(sentinel::engine::PacketPipeline* pipeline);

        signals:
            // 对外暴露给 UI 层的 Qt 信号
            void packetsProcessed(QSharedPointer<QVector<ParsedPacket>> packets);
        void threatDetected(const Alert& alert, const ParsedPacket& packet);
        void statsUpdated(uint64_t bytesProcessed);

    private:
        // 承接引擎回调的内部处理函数
        void onBatchReady(std::shared_ptr<std::vector<ParsedPacket>> batch);
        void onThreatReady(const Alert& alert, const ParsedPacket& packet);
        void onStatsReady(uint64_t bytes);
    };

}