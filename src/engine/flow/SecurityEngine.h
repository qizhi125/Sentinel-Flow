#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <optional>
#include "common/types/NetworkTypes.h"

struct IdsRule {
    int id;
    bool enabled;
    std::string protocol;
    std::string pattern;
    Alert::Level level;
    std::string description;
};

class SecurityEngine {
public:
    static SecurityEngine& instance();

    void addRule(const IdsRule& rule);
    std::vector<IdsRule> getRules() const;

    void addBlacklist(const std::string& ipStr, const std::string& reason);

    std::optional<Alert> inspect(const ParsedPacket& packet);

private:
    SecurityEngine() = default;
    ~SecurityEngine() = default;
    SecurityEngine(const SecurityEngine&) = delete;
    SecurityEngine& operator=(const SecurityEngine&) = delete;

    std::vector<IdsRule> rules;
    mutable std::mutex rulesMutex;

    std::unordered_map<uint32_t, std::string> blacklist;
    std::mutex blacklistMutex;
};