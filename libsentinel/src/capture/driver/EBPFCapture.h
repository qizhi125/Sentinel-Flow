#pragma once
#include "capture/interface/ICaptureDriver.h"
#include "common/queues/SPSCQueue.h"
#include "common/types/NetworkTypes.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sentinel::capture {

// 前置声明：隐藏底层所有的 libbpf / AF_XDP 结构体，防止 C 宏污染 C++ 命名空间
struct XskContext;

class EBPFCapture : public ICaptureDriver {
public:
    static EBPFCapture& instance();

    // 禁用拷贝与移动
    EBPFCapture(const EBPFCapture&) = delete;
    EBPFCapture& operator=(const EBPFCapture&) = delete;

    // 实现 ICaptureDriver 接口
    void init(const std::vector<sentinel::common::SPSCQueue<RawPacket>*>& queues) override;
    void start(const std::string& device) override;
    void stop() override;
    bool setFilter(const std::string& filterExp) override;
    std::vector<std::string> getDeviceList() override;

private:
    EBPFCapture();
    ~EBPFCapture() override;

    void pollWorker(); // AF_XDP 零拷贝收包主循环

    std::vector<sentinel::common::SPSCQueue<RawPacket>*> targetQueues;
    std::string m_device;
    int m_ifindex = -1;

    std::atomic<bool> running{false};
    std::thread captureThread;

    // 利用智能指针管理隐藏的底层上下文
    std::unique_ptr<XskContext> m_ctx;
};

} // namespace sentinel::capture