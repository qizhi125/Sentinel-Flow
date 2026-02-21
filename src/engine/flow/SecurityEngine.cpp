#include "engine/context/ForensicManager.h"
#include "engine/governance/AuditLogger.h"
#include "SecurityEngine.h"
#include <iostream>
#include <regex>
#include <ctime>
#include <cstring>

// 实现单例
SecurityEngine& SecurityEngine::instance() {
    static SecurityEngine instance;
    return instance;
}

void SecurityEngine::addRule(const IdsRule& rule) {
    std::lock_guard<std::mutex> lock(rulesMutex);
    rules.push_back(rule);
}

// 添加 const，匹配头文件
std::vector<IdsRule> SecurityEngine::getRules() const {
    std::lock_guard<std::mutex> lock(rulesMutex);
    return rules;
}

void SecurityEngine::addBlacklist(const std::string& ipStr, const std::string& reason) {
    QHostAddress addr(QString::fromStdString(ipStr));
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        uint32_t ip = addr.toIPv4Address();
        std::lock_guard<std::mutex> lock(blacklistMutex);
        blacklist[ip] = reason;
    }
}

std::optional<Alert> SecurityEngine::inspect(const ParsedPacket& packet) {
    // 1. 检查黑名单
    {
        std::lock_guard<std::mutex> lock(blacklistMutex);
        auto it = blacklist.find(packet.srcIp);
        if (it != blacklist.end()) {
            return Alert{
                static_cast<uint64_t>(time(nullptr)),
                Alert::Critical,
                packet.srcIp,
                "Blocked IP detected: " + it->second,
                "Blacklist"
            };
        }
    }

    // 2. 检查 IDS 规则
    {
        std::lock_guard<std::mutex> lock(rulesMutex);
        if (packet.payloadData.empty()) return std::nullopt;

        std::string payloadStr(packet.payloadData.begin(), packet.payloadData.end());

        for (const auto& rule : rules) {
            if (!rule.enabled) continue;
            if (rule.protocol != "ANY" && rule.protocol != packet.protocol) continue;

            if (payloadStr.find(rule.pattern) != std::string::npos) {
                Alert alert = { static_cast<uint64_t>(time(nullptr)), rule.level, packet.srcIp, rule.description, "Rule-" + std::to_string(rule.id) };

                if (alert.level >= Alert::High) {
                    std::string filename = "evidence_id_" + std::to_string(packet.id) + ".pcap";
                    if (ForensicManager::instance().saveToPcap(packet, filename)) {
                        AuditLogger::instance().log("取证成功: 原始报文已导出至 " + filename, "SUCCESS");
                    }
                }
                return alert;
            }
        }
    }

    // 3. 内置规则示例优化
    if (strcmp(packet.protocol, "HTTP") == 0) {
        std::string payloadStr(packet.payloadData.begin(), packet.payloadData.end());
        if (payloadStr.find("UNION SELECT") != std::string::npos) {
            Alert sqlAlert = { static_cast<uint64_t>(time(nullptr)), Alert::High, packet.srcIp, "SQL Injection Attempt", "SQL_INJECTION_CORE" };

            std::string filename = "evidence_sql_" + std::to_string(packet.id) + ".pcap";
            ForensicManager::instance().saveToPcap(packet, filename);
            AuditLogger::instance().log("检测到 SQL 注入，已触发自动取证: " + filename, "ALERT");

            return sqlAlert;
        }
    }

    return std::nullopt;
}