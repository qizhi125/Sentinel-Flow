#include "DatabaseManager.h"
#include <iostream>
#include <filesystem>
#include <QHostAddress>

DatabaseManager::~DatabaseManager() {
    shutdown();
}

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::shutdown() {
    if (running) {
        running = false;
        queueCv.notify_all();
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool DatabaseManager::init(const std::string& dbPathStr) {
    if (running) return true;

    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path safeDir = fs::temp_directory_path(ec) / "sentinel-flow";
    if (!fs::exists(safeDir)) {
        fs::create_directories(safeDir, ec);
        #ifdef __linux__
                fs::permissions(safeDir, fs::perms::all, fs::perm_options::add, ec);
        #endif
    }

    fs::path dbPath = safeDir / dbPathStr;

    int rc = sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "❌ [Database] 数据库挂载失败: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    #ifdef __linux__
        fs::permissions(dbPath, fs::perms::all, fs::perm_options::add, ec);
    #endif

    char* errMsg = nullptr;
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    const char* sqlAlerts = "CREATE TABLE IF NOT EXISTS alerts ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "timestamp INTEGER,"
                              "rule_name TEXT,"
                              "source_ip TEXT,"
                              "level INTEGER,"
                              "description TEXT);";
    sqlite3_exec(db, sqlAlerts, nullptr, nullptr, nullptr);

    const char* sqlRules = "CREATE TABLE IF NOT EXISTS rules ("
                           "id INTEGER PRIMARY KEY,"
                           "enabled INTEGER,"
                           "protocol TEXT,"
                           "pattern TEXT,"
                           "level INTEGER,"
                           "description TEXT);";
    sqlite3_exec(db, sqlRules, nullptr, nullptr, nullptr);

    const char* sqlBlacklist = "CREATE TABLE IF NOT EXISTS blacklist ("
                               "ip TEXT PRIMARY KEY,"
                               "reason TEXT,"
                               "timestamp INTEGER);";
    sqlite3_exec(db, sqlBlacklist, nullptr, nullptr, nullptr);

    const char* sqlConfig = "CREATE TABLE IF NOT EXISTS config ("
                            "key TEXT PRIMARY KEY,"
                            "value TEXT);";
    sqlite3_exec(db, sqlConfig, nullptr, nullptr, nullptr);

    running = true;
    workerThread = std::thread(&DatabaseManager::workerLoop, this);
    return true;
}

void DatabaseManager::saveRule(const IdsRule& rule) {
    if (!db) return;
    const char* sql = "INSERT OR REPLACE INTO rules (id, enabled, protocol, pattern, level, description) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, rule.id);
        sqlite3_bind_int(stmt, 2, rule.enabled ? 1 : 0);
        sqlite3_bind_text(stmt, 3, rule.protocol.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, rule.pattern.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, rule.level);
        sqlite3_bind_text(stmt, 6, rule.description.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseManager::saveRulesTransaction(const std::vector<IdsRule>& rulesList) {
    if (!db || rulesList.empty()) return;
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    const char* sql = "INSERT OR REPLACE INTO rules (id, enabled, protocol, pattern, level, description) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        for (const auto& r : rulesList) {
            sqlite3_bind_int(stmt, 1, r.id);
            sqlite3_bind_int(stmt, 2, r.enabled ? 1 : 0);
            sqlite3_bind_text(stmt, 3, r.protocol.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, r.pattern.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 5, r.level);
            sqlite3_bind_text(stmt, 6, r.description.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt); // 复位以便下一轮绑定
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
}

void DatabaseManager::deleteRule(int id) {
    if (!db) return;
    const char* sql = "DELETE FROM rules WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseManager::clearRules() {
    if (!db) return;
    sqlite3_exec(db, "DELETE FROM rules;", nullptr, nullptr, nullptr);
}

std::vector<IdsRule> DatabaseManager::loadRules() {
    std::vector<IdsRule> rules;
    if (!db) return rules;
    const char* sql = "SELECT id, enabled, protocol, pattern, level, description FROM rules ORDER BY id ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            IdsRule r;
            r.id = sqlite3_column_int(stmt, 0);
            r.enabled = sqlite3_column_int(stmt, 1) != 0;
            const unsigned char* proto = sqlite3_column_text(stmt, 2);
            r.protocol = proto ? reinterpret_cast<const char*>(proto) : "ANY";
            const unsigned char* patt = sqlite3_column_text(stmt, 3);
            r.pattern = patt ? reinterpret_cast<const char*>(patt) : "";
            r.level = static_cast<Alert::Level>(sqlite3_column_int(stmt, 4));
            const unsigned char* desc = sqlite3_column_text(stmt, 5);
            r.description = desc ? reinterpret_cast<const char*>(desc) : "";
            rules.push_back(r);
        }
        sqlite3_finalize(stmt);
    }
    return rules;
}

void DatabaseManager::saveBlacklist(const std::string& ip, const std::string& reason) {
    if (!db) return;
    const char* sql = "INSERT OR REPLACE INTO blacklist (ip, reason, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, std::time(nullptr));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void DatabaseManager::deleteBlacklist(const std::string& ip) {
    if (!db) return;
    const char* sql = "DELETE FROM blacklist WHERE ip = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<std::pair<std::string, std::string>> DatabaseManager::loadBlacklist() {
    std::vector<std::pair<std::string, std::string>> bl;
    if (!db) return bl;
    const char* sql = "SELECT ip, reason FROM blacklist ORDER BY timestamp DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* ip = sqlite3_column_text(stmt, 0);
            const unsigned char* reason = sqlite3_column_text(stmt, 1);
            bl.emplace_back(
                ip ? reinterpret_cast<const char*>(ip) : "",
                reason ? reinterpret_cast<const char*>(reason) : ""
            );
        }
        sqlite3_finalize(stmt);
    }
    return bl;
}

void DatabaseManager::saveConfig(const std::string& key, const std::string& value) {
    if (!db) return;
    const char* sql = "INSERT OR REPLACE INTO config (key, value) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string DatabaseManager::loadConfig(const std::string& key, const std::string& defaultVal) {
    if (!db) return defaultVal;
    std::string result = defaultVal;
    const char* sql = "SELECT value FROM config WHERE key = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* val = sqlite3_column_text(stmt, 0);
            if (val) result = reinterpret_cast<const char*>(val);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void DatabaseManager::saveAlert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (alertQueue.size() >= 20000) {
            return;
        }
        alertQueue.push(alert);
    }
    queueCv.notify_one();
}

void DatabaseManager::workerLoop() {
    while (running) {
        std::vector<Alert> batch;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] { return !alertQueue.empty() || !running; });

            if (!running && alertQueue.empty()) break;

            int count = 0;
            while (!alertQueue.empty() && count < 1000) {
                batch.push_back(alertQueue.front());
                alertQueue.pop();
                count++;
            }
        }

        if (batch.empty()) continue;
        if (!db) continue;

        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "[DB] Begin transaction failed: " << (errMsg ? errMsg : "") << std::endl;
            sqlite3_free(errMsg);
            continue;
        }

        const char* sql = "INSERT INTO alerts (timestamp, rule_name, source_ip, level, description) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;

        bool success = true;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            for (const auto& alert : batch) {
                sqlite3_bind_int64(stmt, 1, alert.timestamp);
                sqlite3_bind_text(stmt, 2, alert.ruleName.c_str(), -1, SQLITE_STATIC);

                std::string ipStr = QHostAddress(alert.sourceIp).toString().toStdString();
                sqlite3_bind_text(stmt, 3, ipStr.c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_bind_int(stmt, 4, static_cast<int>(alert.level));
                sqlite3_bind_text(stmt, 5, alert.description.c_str(), -1, SQLITE_STATIC);

                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    success = false;
                    std::cerr << "[DB] Insert failed: " << sqlite3_errmsg(db) << std::endl;
                }
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        } else
            success = false;

        if (success) {
            rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::cerr << "[DB] Commit failed: " << (errMsg ? errMsg : "") << std::endl;
            }
        } else {
            rc = sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::cerr << "[DB] Rollback failed: " << (errMsg ? errMsg : "") << std::endl;
            }
        }
        sqlite3_free(errMsg);
    }
}

std::vector<Alert> DatabaseManager::loadRecentAlerts(int limit) {
    std::vector<Alert> alerts;
    if (!db) return alerts;

    const char* sql = "SELECT timestamp, rule_name, source_ip, level, description FROM alerts ORDER BY timestamp DESC LIMIT ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Alert a;
            a.timestamp = sqlite3_column_int64(stmt, 0);
            const unsigned char* ruleText = sqlite3_column_text(stmt, 1);
            a.ruleName = ruleText ? reinterpret_cast<const char*>(ruleText) : "Unknown";

            const unsigned char* ipText = sqlite3_column_text(stmt, 2);
            if (ipText) {
                a.sourceIp = QHostAddress(QString::fromUtf8(reinterpret_cast<const char*>(ipText))).toIPv4Address();
            } else {
                a.sourceIp = 0;
            }

            a.level = static_cast<Alert::Level>(sqlite3_column_int(stmt, 3));
            const unsigned char* descText = sqlite3_column_text(stmt, 4);
            a.description = descText ? reinterpret_cast<const char*>(descText) : "";
            alerts.push_back(a);
        }
        sqlite3_finalize(stmt);
    }
    return alerts;
}
