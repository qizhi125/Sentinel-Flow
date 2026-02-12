#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include "common/types/NetworkTypes.h"

class DatabaseManager {
public:
    static DatabaseManager& instance();

    bool init(const std::string& dbPath = "sentinel_data.db");
    void shutdown();

    void saveAlert(const Alert& alert);
    std::vector<Alert> loadRecentAlerts(int limit = 100);

private:
    DatabaseManager() = default;
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    void workerLoop();

    sqlite3* db = nullptr;

    std::queue<Alert> alertQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::thread workerThread;
    std::atomic<bool> running{false};
};