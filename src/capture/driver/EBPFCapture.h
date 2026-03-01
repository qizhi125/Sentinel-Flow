#pragma once
#include "capture/interface/ICaptureDriver.h"
#include <string>
#include <vector>

namespace sentinel::capture {

    class EBPFCapture : public ICaptureDriver {
    public:
        void init(const std::vector<PacketQueue*>& queues) override {
            // 暂未实现
        }

        void start(const std::string& device) override {
            // 暂未实现
        }

        void stop() override {
            // 暂未实现
        }

        bool setFilter(const std::string& filterExp) override {
            return false;
        }

        std::vector<std::string> getDeviceList() override {
            return {};
        }
    };

}