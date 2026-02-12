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
    fs::path dbPath = fs::current_path() / dbPathStr;

    int rc = sqlite3_open(dbPath.string().c_str(), &db);
    if (rc) {
        std::cerr << "❌ [Database] Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);

    const char* sql = "CREATE TABLE IF NOT EXISTS alerts ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "timestamp INTEGER,"
                          "rule_name TEXT,"
                          "source_ip TEXT,"
                          "level INTEGER,"
                          "description TEXT);";

    char* zErrMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "❌ [Database] SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
        return false;
    }

    std::cout << "✅ [Database] Initialized async worker at: " << dbPath << std::endl;

    running = true;
    workerThread = std::thread(&DatabaseManager::workerLoop, this);

    return true;
}

void DatabaseManager::saveAlert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (alertQueue.size() > 5000) {
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
            while (!alertQueue.empty() && count < 50) {
                batch.push_back(alertQueue.front());
                alertQueue.pop();
                count++;
            }
        }

        if (batch.empty()) continue;

        if (!db) continue;

        char* errMsg = 0;
        sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &errMsg);

        const char* sql = "INSERT INTO alerts (timestamp, rule_name, source_ip, level, description) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            for (const auto& alert : batch) {
                sqlite3_bind_int64(stmt, 1, alert.timestamp);
                sqlite3_bind_text(stmt, 2, alert.ruleName.c_str(), -1, SQLITE_STATIC);

                std::string ipStr = QHostAddress(alert.sourceIp).toString().toStdString();
                sqlite3_bind_text(stmt, 3, ipStr.c_str(), -1, SQLITE_TRANSIENT);

                sqlite3_bind_int(stmt, 4, static_cast<int>(alert.level));
                sqlite3_bind_text(stmt, 5, alert.description.c_str(), -1, SQLITE_STATIC);

                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_exec(db, "COMMIT;", 0, 0, &errMsg);
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