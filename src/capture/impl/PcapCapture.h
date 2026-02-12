#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <thread>
#include "common/queues/ThreadSafeQueue.h"
#include "common/types/NetworkTypes.h"

class PcapCapture {
public:
    static PcapCapture& instance() {
        static PcapCapture instance;
        return instance;
    }

    // 初始化：绑定多个 Worker 队列
    void init(const std::vector<ThreadSafeQueue<RawPacket>*>& queues);

    // 开始捕获
    void start(const std::string& device);

    // 停止捕获
    void stop();

    // 设置 BPF 过滤器
    static bool setFilter(const std::string& filterExp);

    // 获取设备列表
    static std::vector<std::string> getDeviceList();

private:
    PcapCapture() = default;
    ~PcapCapture();

    // 简单的 IP Hash 分流算法
    int hashPacket(const uint8_t* data, int len);

    void captureLoop();

    std::atomic<bool> running{false};
    std::thread captureThread;
    std::string currentDevice;

    // 多队列支持
    std::vector<ThreadSafeQueue<RawPacket>*> workerQueues;
    size_t queueCount = 0;
};