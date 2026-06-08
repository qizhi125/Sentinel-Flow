#include "sentinel/engine/match/AhoCorasick.h"

#include <algorithm>
#include <cctype>
#include <queue>

namespace sentinel::engine {

// ---- 大小写转换 ----

uint8_t AhoCorasick::to_upper(uint8_t c) noexcept {
    return static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
}

// ---- 插入模式串 ----

void AhoCorasick::insert(std::string_view pattern, int32_t rule_id) {
    if (pattern.empty()) {
        return;
    }

    int32_t curr = kRoot;
    for (char ch : pattern) {
        uint8_t const idx = to_upper(static_cast<uint8_t>(ch));
        if (nodes_[curr].next[idx] == kInvalid) {
            nodes_[curr].next[idx] = static_cast<int32_t>(nodes_.size());
            nodes_.emplace_back();
        }
        curr = nodes_[curr].next[idx];
    }
    nodes_[curr].rule_ids.push_back(rule_id);
}

// ---- 构建失败指针与全跃迁表 ----

void AhoCorasick::build() {
    std::queue<int32_t> q;

    // 第一层：root 的直接子节点
    for (int i = 0; i < kAlphabetSize; ++i) {
        int32_t& next = nodes_[kRoot].next[i];
        if (next != kInvalid) {
            nodes_[next].fail = kRoot;
            q.push(next);
        } else {
            next = kRoot; // root 缺失跃迁指向自身
        }
    }

    // BFS 逐层构建
    while (!q.empty()) {
        int32_t const u = q.front();
        q.pop();

        for (int i = 0; i < kAlphabetSize; ++i) {
            int32_t& next = nodes_[u].next[i];

            if (next != kInvalid) {
                // 子节点存在：fail = fail 父节点的第 i 跃迁
                nodes_[next].fail = nodes_[nodes_[u].fail].next[i];

                // 继承 fail 节点的规则 ID（后缀匹配）
                auto& fail_ids = nodes_[nodes_[next].fail].rule_ids;
                if (!fail_ids.empty()) {
                    auto& target = nodes_[next].rule_ids;
                    target.insert(target.end(), fail_ids.begin(), fail_ids.end());
                }

                q.push(next);
            } else {
                // 子节点缺失：跃迁 = fail 父节点的第 i 跃迁
                next = nodes_[nodes_[u].fail].next[i];
            }
        }
    }

    built_ = true;
}

// ---- 匹配（调用方提供输出缓冲） ----

bool AhoCorasick::match(std::span<const uint8_t> payload,
                        std::vector<int32_t>& out) const {
    int32_t curr = kRoot;

    for (uint8_t const c : payload) {
        curr = nodes_[curr].next[to_upper(c)];

        auto const& ids = nodes_[curr].rule_ids;
        if (!ids.empty()) {
            out.insert(out.end(), ids.begin(), ids.end());
        }
    }

    return !out.empty();
}

// ---- 匹配（thread_local 便捷接口） ----

const std::vector<int32_t>* AhoCorasick::match(std::span<const uint8_t> payload) const {
    thread_local std::vector<int32_t> t_matches;
    t_matches.clear();

    if (match(payload, t_matches)) {
        return &t_matches;
    }
    return nullptr;
}

} // namespace sentinel::engine
