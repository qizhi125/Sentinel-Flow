#include "engine/workers/ForensicWorker.h"
#include "engine/governance/AuditLogger.h"
#include "engine/workers/ForensicWorker.h"
#include "common/utils/StringUtils.h"
#include "capture/impl/PcapCapture.h"
#include "SecurityEngine.h"
#include <arpa/inet.h>
#include <QStringList>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <algorithm>

SecurityEngine& SecurityEngine::instance() {
    static SecurityEngine instance;
    return instance;
}

SecurityEngine::SecurityEngine() {
    std::filesystem::create_directories("evidences");
    acDetector = std::make_unique<sentinel::engine::AhoCorasick>();
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
    // 获取规则快照
    std::vector<IdsRule> currentRules;
    {
        std::shared_lock lock(rulesMutex);
        currentRules = rules;
    }

    // 在锁外部构建全新的 AC 自动机
    auto newDetector = std::make_unique<sentinel::engine::AhoCorasick>();
    for (const auto& rule : currentRules) {
        if (rule.enabled && !rule.pattern.empty()) {
            newDetector->insert(rule.pattern, rule.id);
        }
    }
    newDetector->build();

    // 极速切换指针
    std::unique_ptr<sentinel::engine::AhoCorasick> oldDetector;
    {
        std::unique_lock lock(rulesMutex);
        oldDetector = std::move(acDetector); // 将旧引擎转移到局部变量
        acDetector = std::move(newDetector); // 挂载新引擎
    }

    // 4. 旧引擎的销毁发生在锁外部 (脱机销毁)
    // 当 oldDetector 离开作用域时，几万个树节点的 delete 操作被触发
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
        if (!acDetector) return std::nullopt;
        hitRuleIds = acDetector->match(packet.payloadData);

        if (hitRuleIds && !hitRuleIds->empty()) {
            const IdsRule& rule = rulesMap[hitRuleIds->at(0)];

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

    QStringList bpfRules;
    for (uint32_t ip : m_blacklistedIps) {
        struct in_addr ip_addr;
        ip_addr.s_addr = htonl(ip);
        QString ipStr = QString::fromStdString(inet_ntoa(ip_addr));
        bpfRules.append(QString("not host %1").arg(ipStr));
    }

    QString finalBpf = bpfRules.join(" and ");
    PcapCapture::instance().setFilter(finalBpf.toStdString());
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