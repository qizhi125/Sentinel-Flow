#include <gtest/gtest.h>
#include "sentinel/engine/DatabaseManager.h"

#include <filesystem>

using namespace sentinel::engine;

// 测试数据库路径
static const char* kTestDbPath = "test_alerts.db";

// 测试结束后清理
class DatabaseManagerTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::filesystem::remove(kTestDbPath);
        std::filesystem::remove((std::string(kTestDbPath) + "-wal").c_str());
        std::filesystem::remove((std::string(kTestDbPath) + "-shm").c_str());
    }
};

// ---- 数据库创建与表初始化 ----
TEST_F(DatabaseManagerTest, CreateDatabase) {
    DatabaseManager db(kTestDbPath);
    // 构造函数同步创建数据库文件和表
    EXPECT_TRUE(std::filesystem::exists(kTestDbPath));
    EXPECT_GE(db.total_alerts(), 0);
}

// ---- 异步保存 + 同步冲刷 + 查询 ----
TEST_F(DatabaseManagerTest, SaveAndQueryAlerts) {
    DatabaseManager db(kTestDbPath);

    db.save_alert(1001, "SQL Injection detected in HTTP request");
    db.save_alert(1002, "XSS payload found");
    db.save_alert(1003, "Reverse shell connection detected");

    // 确定性冲刷：等待后台线程将所有告警写入 SQLite
    db.flush();

    auto alerts = db.recent_alerts(10);
    EXPECT_EQ(alerts.size(), 3u);
    EXPECT_EQ(db.total_alerts(), 3);
}

// ---- 大批量插入（背压测试） ----
TEST_F(DatabaseManagerTest, BulkInsertBackpressure) {
    DatabaseManager db(kTestDbPath);

    constexpr int kNumAlerts = 2000;
    for (int i = 0; i < kNumAlerts; ++i) {
        db.save_alert(i, "bulk test alert payload");
    }

    db.flush();

    EXPECT_EQ(db.total_alerts(), kNumAlerts);
}

// ---- 正常关闭冲刷 ----
TEST_F(DatabaseManagerTest, ShutdownFlush) {
    {
        DatabaseManager db(kTestDbPath);
        db.save_alert(1, "alert before shutdown");
        db.save_alert(2, "another alert");
        // 析构自动调用 shutdown → 最后一批数据落盘
    }

    // 重新打开验证持久化
    {
        DatabaseManager db2(kTestDbPath);
        auto alerts = db2.recent_alerts(10);
        EXPECT_EQ(alerts.size(), 2u);
    }
}

// ---- 空数据库查询 ----
TEST_F(DatabaseManagerTest, EmptyDatabaseQuery) {
    DatabaseManager db(kTestDbPath);

    auto alerts = db.recent_alerts(100);
    EXPECT_TRUE(alerts.empty());
    EXPECT_EQ(db.total_alerts(), 0);
}

// ---- flush 幂等性 ----
TEST_F(DatabaseManagerTest, FlushIdempotent) {
    DatabaseManager db(kTestDbPath);

    // 空缓冲 flush 不阻塞
    db.flush();
    db.flush();
    EXPECT_EQ(db.total_alerts(), 0);
}

// ---- WAL 文件生成 ----
TEST_F(DatabaseManagerTest, WalFileCreated) {
    DatabaseManager db(kTestDbPath);
    db.save_alert(1, "test WAL");
    db.flush();

    // WAL 模式应生成 -wal 和 -shm 文件
    std::string const wal_path = std::string(kTestDbPath) + "-wal";
    std::string const shm_path = std::string(kTestDbPath) + "-shm";
    EXPECT_TRUE(std::filesystem::exists(wal_path) || std::filesystem::exists(shm_path));
}
