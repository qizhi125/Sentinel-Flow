#include "SecurityEngine.h"
#include "engine/workers/ForensicWorker.h"
#include "engine/governance/AuditLogger.h"
#include "common/utils/StringUtils.h"
#include "capture/impl/PcapCapture.h"
#include <arpa/inet.h>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <sstream>

SecurityEngine& SecurityEngine::instance() {
    static SecurityEngine instance;
    return instance;
}

SecurityEngine::SecurityEngine() {
    std::error_code ec;
    std::filesystem::create_directories("evidences", ec);
    acDetector = std::make_unique<sentinel::engine::AhoCorasick>();
    compileRules(); 
    forensicWorker_.start();
}

SecurityEngine::~SecurityEngine() {
    forensicWorker_.stop();
}

void SecurityEngine::addRule(const IdsRule& rule) {
    std::unique_lock lock(rulesMutex);
    rules.push_back(rule);
    rulesMap[rule.id] = rule;
}

void SecurityEngine::compileRules() {
    std::vector<IdsRule> currentRules;
    {
        std::shared_lock lock(rulesMutex);
        currentRules = rules;
    }

    auto newDetector = std::make_unique<sentinel::engine::AhoCorasick>();
    for (const auto& rule : currentRules) {
        if (rule.enabled && !rule.pattern.empty()) {
            newDetector->insert(rule.pattern, rule.id);
        }
    }
    newDetector->build();

    std::unique_ptr<sentinel::engine::AhoCorasick> oldDetector;
    {
        std::unique_lock lock(rulesMutex);
        oldDetector = std::move(acDetector);
        acDetector = std::move(newDetector);
    }
}

std::vector<IdsRule> SecurityEngine::getRules() const {
    std::shared_lock lock(rulesMutex);
    return rules;
}

void SecurityEngine::cleanupSuppressionCache(uint64_t currentMs) {
    std::unique_lock lock(suppressionMutex);
    for (auto it = suppressionCache.begin(); it != suppressionCache.end(); ) {
        if (currentMs - it->second > SUPPRESSION_WINDOW_MS * 2) {
            it = suppressionCache.erase(it);
        } else {
            ++it;
        }
    }
    lastCacheCleanupTime = currentMs;
}

std::optional<Alert> SecurityEngine::inspect(const ParsedPacket& packet) {
    if (packet.payloadData.empty()) return std::nullopt;

    const std::vector<int>* hitRuleIds = nullptr;
    {
        std::shared_lock lock(rulesMutex);
        
        // 关键修复 1：阻断空状态下的匹配逻辑
        if (rules.empty() || !acDetector) return std::nullopt;
        
        hitRuleIds = acDetector->match(packet.payloadData);

        if (hitRuleIds && !hitRuleIds->empty()) {
            // 关键修复 2：使用线程安全的 find() 替代 operator[]
            auto it = rulesMap.find(hitRuleIds->at(0));
            if (it == rulesMap.end()) return std::nullopt;
            const IdsRule& rule = it->second;

            if (rule.protocol == "ANY" || rule.protocol == packet.protocol) {
                uint64_t currentMs = packet.timestamp;
                std::string cacheKey = std::to_string(packet.srcIp) + "_" + std::to_string(rule.id);

                {
                    std::shared_lock readLock(suppressionMutex);
                    auto cacheIt = suppressionCache.find(cacheKey);
                    if (cacheIt != suppressionCache.end() && (currentMs - cacheIt->second) < SUPPRESSION_WINDOW_MS) {
                        return std::nullopt;
                    }
                }

                {
                    std::unique_lock writeLock(suppressionMutex);
                    suppressionCache[cacheKey] = currentMs;
                }

                if (currentMs - lastCacheCleanupTime > 10000) {
                    cleanupSuppressionCache(currentMs);
                }

                Alert alert = {
                    static_cast<uint64_t>(time(nullptr)),
                    rule.level,
                    packet.srcIp,
                    rule.description,
                    "RULE-" + std::to_string(rule.id)
                };

                if (alert.level >= Alert::High) {
                    forensicWorker_.enqueue(packet);
                }
                return alert;
            }
        }
    }
    return std::nullopt;
}

void SecurityEngine::blockIp(uint32_t ip) {
    {
        std::unique_lock lock(m_blacklistMutex);
        m_blacklistedIps.insert(ip);
    }
    syncBlacklistToBpf();
}

void SecurityEngine::unblockIp(uint32_t ip) {
    {
        std::unique_lock lock(m_blacklistMutex);
        m_blacklistedIps.erase(ip);
    }
    syncBlacklistToBpf();
}

bool SecurityEngine::isIpBlocked(uint32_t ip) const {
    std::shared_lock lock(m_blacklistMutex);
    return m_blacklistedIps.find(ip) != m_blacklistedIps.end();
}

void SecurityEngine::syncBlacklistToBpf() {
    std::shared_lock lock(m_blacklistMutex);

    if (m_blacklistedIps.empty()) {
        PcapCapture::instance().setFilter("");
        return;
    }

    if (m_blacklistedIps.size() > 50) {
        PcapCapture::instance().setFilter("");
        return;
    }

    std::vector<std::string> bpfRules;
    for (uint32_t ip : m_blacklistedIps) {
        struct in_addr ip_addr;
        ip_addr.s_addr = htonl(ip);
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, buf, INET_ADDRSTRLEN);
        bpfRules.push_back("not host " + std::string(buf));
    }

    std::string finalBpf;
    for (size_t i = 0; i < bpfRules.size(); ++i) {
        finalBpf += bpfRules[i];
        if (i < bpfRules.size() - 1) {
            finalBpf += " and ";
        }
    }
    
    PcapCapture::instance().setFilter(finalBpf);
}

void SecurityEngine::clearRules() {
    std::unique_lock lock(rulesMutex);
    rules.clear();
    rulesMap.clear();
}

void SecurityEngine::removeRule(int ruleId) {
    std::unique_lock lock(rulesMutex);
    rules.erase(std::remove_if(rules.begin(), rules.end(),
        [ruleId](const IdsRule& r) { return r.id == ruleId; }), rules.end());
    rulesMap.erase(ruleId);
}

