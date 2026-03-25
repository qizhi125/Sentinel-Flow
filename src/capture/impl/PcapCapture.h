#pragma once
#include "capture/interface/ICaptureDriver.h"
#include "common/queues/SPSCQueue.h"
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <pcap.h>

class PcapCapture : public sentinel::capture::ICaptureDriver {
public:
    static PcapCapture& instance() {
        static PcapCapture instance;
        return instance;
    }

    void init(const std::vector<sentinel::capture::PacketQueue*>& queues) override;
    void start(const std::string& device) override;
    void stop() override;

    bool setFilter(const std::string& filterExp) override;
    std::vector<std::string> getDeviceList() override;

private:
    PcapCapture() = default;
    ~PcapCapture() override;

    int hashPacket(const uint8_t* data, int len, uint32_t offset);
    void captureLoop();

    std::atomic<bool> running{false};
    std::thread captureThread;
    std::string currentDevice;

    std::vector<sentinel::capture::PacketQueue*> workerQueues;
    size_t queueCount = 0;

    mutable std::shared_mutex handleMutex;
    pcap_t* handle = nullptr;
};