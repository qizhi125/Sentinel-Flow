#pragma once
#include "common/types/NetworkTypes.h"
#include "engine/flow/SecurityEngine.h"
#include <string>
#include <vector>
#include <sqlite3.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

class DatabaseManager {
public:
    static DatabaseManager& instance();

    bool init(const std::string& dbPath = "sentinel_data.db");
    void shutdown();

    void saveAlert(const Alert& alert);
    std::vector<Alert> loadRecentAlerts(int limit = 100);

    void saveRule(const IdsRule& rule);
    void saveRulesTransaction(const std::vector<IdsRule>& rulesList);
    void deleteRule(int id);
    void clearRules();
    std::vector<IdsRule> loadRules();

    void saveBlacklist(const std::string& ip, const std::string& reason);
    void deleteBlacklist(const std::string& ip);
    std::vector<std::pair<std::string, std::string>> loadBlacklist();

    void saveConfig(const std::string& key, const std::string& value);
    std::string loadConfig(const std::string& key, const std::string& defaultVal = "");

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

    mutable std::mutex dbMutex;
};