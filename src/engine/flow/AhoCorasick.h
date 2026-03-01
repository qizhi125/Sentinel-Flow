#pragma once
#include <vector>
#include <queue>
#include <string>
#include <memory>
#include <cstdint>
#include <cstring>

namespace sentinel::engine {

class AhoCorasick {
public:
    struct Node {
        Node* next[256];
        Node* fail = nullptr;
        std::vector<int> ruleIds;
        Node() { std::memset(next, 0, sizeof(next)); }
    };

    AhoCorasick() : root(std::make_unique<Node>()) {}

    void insert(const std::string& pattern, int ruleId) {
        if (pattern.empty()) return;
        Node* curr = root.get();
        for (uint8_t c : pattern) {
            uint8_t idx = static_cast<uint8_t>(std::toupper(c));
            if (!curr->next[idx]) {
                auto newNode = std::make_unique<Node>();
                curr->next[idx] = newNode.get();
                allNodes.push_back(std::move(newNode));
            }
            curr = curr->next[idx];
        }
        curr->ruleIds.push_back(ruleId);
    }

    void build() {
        std::queue<Node*> q;
        for (int i = 0; i < 256; ++i) {
            if (root->next[i]) {
                root->next[i]->fail = root.get();
                q.push(root->next[i]);
            } else {
                root->next[i] = root.get();
            }
        }

        while (!q.empty()) {
            Node* u = q.front(); q.pop();
            for (int i = 0; i < 256; ++i) {
                if (u->next[i]) {
                    u->next[i]->fail = u->fail->next[i];
                    auto& targetIds = u->next[i]->ruleIds;
                    auto& failIds = u->next[i]->fail->ruleIds;
                    if (!failIds.empty()) {
                        targetIds.insert(targetIds.end(), failIds.begin(), failIds.end());
                    }
                    q.push(u->next[i]);
                } else {
                    u->next[i] = u->fail->next[i];
                }
            }
        }
    }

    const std::vector<int>* match(const std::vector<uint8_t>& data) const {
        if (!root) return nullptr;
        Node* curr = root.get();
        for (uint8_t c : data) {
            curr = curr->next[static_cast<uint8_t>(std::toupper(c))];
            if (!curr->ruleIds.empty()) return &curr->ruleIds;
        }
        return nullptr;
    }

private:
    std::unique_ptr<Node> root;
    std::vector<std::unique_ptr<Node>> allNodes;
};

}