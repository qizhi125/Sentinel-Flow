#pragma once
#include <string>
#include <iostream>
#include <mutex>
#include <functional>
#include <vector>

class AuditLogger {
public:
    static AuditLogger& instance() {
        static AuditLogger instance;
        return instance;
    }

    using LogCallback = std::function<void(const std::string&, const std::string&)>;

    void addCallback(LogCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(cb);
    }

    void log(const std::string& message, const std::string& type = "INFO") {
        std::cout << "[" << type << "] " << message << std::endl;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& cb : callbacks_) {
            if (cb) cb(message, type);
        }
    }

private:
    AuditLogger() = default;
    std::vector<LogCallback> callbacks_;
    std::mutex mutex_;
};