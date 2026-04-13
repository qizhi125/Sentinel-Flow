#pragma once
#include "capture/interface/ICaptureDriver.h"
#include "common/queues/SPSCQueue.h"
#include <atomic>
#include <pcap.h>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

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

    void setOfflineMode(bool offline) {
        isOffline = offline;
    }
    void setVerbose(bool v) {
        isVerbose = v;
    }

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
    bool isOffline = false;
    bool isVerbose = false;

    mutable std::shared_mutex handleMutex;
    pcap_t* handle = nullptr;
};
