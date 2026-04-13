#pragma once
#include "common/queues/SPSCQueue.h"
#include "common/types/NetworkTypes.h"
#include "engine/interface/IInspector.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace sentinel::engine {
class PacketPipeline {
public:
    // 定义回调契约替代 Qt 信号
    using BatchCallback = std::function<void(std::shared_ptr<std::vector<ParsedPacket>>)>;
    using ThreatCallback = std::function<void(const Alert&, const ParsedPacket&)>;
    using StatsCallback = std::function<void(uint64_t)>;

    explicit PacketPipeline();
    ~PacketPipeline();

    // 禁用拷贝与移动构造，确保线程安全
    PacketPipeline(const PacketPipeline&) = delete;
    PacketPipeline& operator=(const PacketPipeline&) = delete;

    void setInputQueue(sentinel::common::SPSCQueue<RawPacket>* queue);
    void setInspector(sentinel::engine::IInspector* inspector);
    void setCoreId(int coreId);

    // 依赖注入回调接口
    void setCallBack(BatchCallback batchCb, ThreatCallback threatCb, StatsCallback statsCb);

    void startPipeline();
    void stopPipeline();
    void wait();            // 暴露显式等待接口，兼容原有生命周期管理
    bool isRunning() const; // 暴露管线运行状态

private:
    void run();
    void flushBatch(uint64_t& bytesAccumulator);

    sentinel::common::SPSCQueue<RawPacket>* inputQueue = nullptr;
    sentinel::engine::IInspector* m_inspector = nullptr;

    std::atomic<bool> running{false};
    int m_coreId = -1;
    std::thread workerThread;

    std::shared_ptr<std::vector<ParsedPacket>> currentBatch;

    BatchCallback m_batchCb;
    ThreatCallback m_threatCb;
    StatsCallback m_statsCb;
};
} // namespace sentinel::engine
