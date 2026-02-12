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

        // 简单载荷转 string (仍需优化，但已比之前好)
        std::string payloadStr(packet.payloadData.begin(), packet.payloadData.end());

        for (const auto& rule : rules) {
            if (!rule.enabled) continue;
            // 简单的协议匹配
            if (rule.protocol != "ANY" && rule.protocol != packet.protocol) continue;

            if (payloadStr.find(rule.pattern) != std::string::npos) {
                return Alert{
                    static_cast<uint64_t>(time(nullptr)),
                    rule.level,
                    packet.srcIp,
                    rule.description,
                    "Rule-" + std::to_string(rule.id)
                };
            }
        }
    }

    // 3. 内置规则示例
    if (strcmp(packet.protocol, "HTTP") == 0) {
        std::string payloadStr(packet.payloadData.begin(), packet.payloadData.end());
        if (payloadStr.find("Union Select") != std::string::npos) {
            return Alert{
                static_cast<uint64_t>(time(nullptr)),
                Alert::High,
                packet.srcIp,
                "SQL Injection Attempt",
                "SQL_INJECTION_CORE"
            };
        }
    }

    return std::nullopt;
}