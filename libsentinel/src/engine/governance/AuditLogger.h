#pragma once
#include <ctime>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

class AuditLogger {
public:
    using Callback = std::function<void(const std::string&, const std::string&)>;

    static AuditLogger& instance() {
        static AuditLogger instance;
        return instance;
    }

    void log(const std::string& message, const std::string& type = "INFO") {
        std::vector<Callback> localCallbacks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.push_back({std::time(nullptr), message, type});
            localCallbacks = callbacks_;
        }

        std::cout << "[" << type << "] " << message << std::endl;
        for (const auto& cb : localCallbacks) {
            cb(message, type);
        }
    }

    void addCallback(Callback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(cb);
    }

    struct Entry {
        std::time_t timestamp;
        std::string message;
        std::string type;
    };

    std::vector<Entry> getRecent(int limit = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.size() <= static_cast<size_t>(limit))
            return entries_;
        return std::vector<Entry>(entries_.end() - limit, entries_.end());
    }

private:
    AuditLogger() = default;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::vector<Callback> callbacks_;
};