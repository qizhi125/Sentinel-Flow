#pragma once
#include "AhoCorasick.h"
#include "common/types/NetworkTypes.h"
#include "engine/interface/IInspector.h"
#include "engine/workers/ForensicWorker.h"
#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct IdsRule {
    int id;
    bool enabled;
    std::string protocol;
    std::string pattern;
    Alert::Level level;
    std::string description;
};

class SecurityEngine : public sentinel::engine::IInspector {
public:
    static SecurityEngine& instance();
    void compileRules();

    void addRule(const IdsRule& rule);
    std::vector<IdsRule> getRules() const;

    std::optional<Alert> inspect(const ParsedPacket& packet) override;

    void blockIp(uint32_t ip);
    void unblockIp(uint32_t ip);
    bool isIpBlocked(uint32_t ip) const;
    void syncBlacklistToBpf();
    void clearRules();
    void removeRule(int ruleId);

private:
    SecurityEngine();
    ~SecurityEngine() override;
    SecurityEngine(const SecurityEngine&) = delete;
    SecurityEngine& operator=(const SecurityEngine&) = delete;

    std::unordered_map<int, IdsRule> rulesMap;
    std::vector<IdsRule> rules;
    mutable std::shared_mutex rulesMutex;

    std::unique_ptr<sentinel::engine::AhoCorasick> acDetector;

    ForensicWorker forensicWorker_;

    std::unordered_map<std::string, uint64_t> suppressionCache;
    mutable std::shared_mutex suppressionMutex;
    static constexpr uint64_t SUPPRESSION_WINDOW_MS = 2000;

    std::atomic<uint64_t> lastCacheCleanupTime{0};
    void cleanupSuppressionCache(uint64_t currentMs);

    std::unordered_set<uint32_t> m_blacklistedIps;
    mutable std::shared_mutex m_blacklistMutex;
};
