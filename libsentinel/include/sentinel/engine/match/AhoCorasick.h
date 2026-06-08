#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sentinel::engine {

// Aho-Corasick 多模式匹配自动机。
//
// 节点跃迁使用 int32_t 索引（指向内部 nodes_ 向量），
// 避免堆碎片的 Node* 指针追逐，提升 CPU 缓存局部性。
// 匹配接口接受 std::span<const uint8_t>，零拷贝扫描网络载荷。
class AhoCorasick {
public:
    static constexpr int32_t kRoot = 0;
    static constexpr int32_t kInvalid = -1;
    static constexpr int32_t kAlphabetSize = 256;

    // 自动机节点。
    struct Node {
        int32_t next[kAlphabetSize]{}; // 转移表（build 前 -1 = 无转移，build 后全部有效）
        int32_t fail = kInvalid;        // 失败指针索引（-1 = 无/root）
        std::vector<int32_t> rule_ids;  // 以当前节点结尾的规则 ID 列表

        Node() {
            for (int32_t& n : next) {
                n = kInvalid;
            }
        }
    };

    AhoCorasick() {
        nodes_.reserve(256);
        nodes_.emplace_back();          // nodes_[0] = root
        nodes_[kRoot].fail = kRoot;     // root 失败指针指向自身
    }

    // 插入模式串（自动转为大写，实现大小写不敏感匹配）。
    // rule_id 为关联的规则 ID，在匹配成功时返回。
    void insert(std::string_view pattern, int32_t rule_id);

    // 构建失败指针和跃迁表。必须在所有 insert() 之后、首次 match() 之前调用。
    void build();

    // 扫描载荷，收集所有命中的规则 ID 存入 out（不清空，追加）。
    // 返回 true 表示至少命中一条规则。
    [[nodiscard]] bool match(std::span<const uint8_t> payload,
                             std::vector<int32_t>& out) const;

    // 便捷接口：返回 thread_local 缓存向量的指针。
    // 返回 nullptr 表示无命中。
    [[nodiscard]] const std::vector<int32_t>* match(std::span<const uint8_t> payload) const;

    // 自动机是否已构建。
    [[nodiscard]] bool is_built() const noexcept { return built_; }

    // 节点总数（含 root）。
    [[nodiscard]] size_t node_count() const noexcept { return nodes_.size(); }

private:
    [[nodiscard]] static uint8_t to_upper(uint8_t c) noexcept;

    std::vector<Node> nodes_;
    bool built_{false};
};

} // namespace sentinel::engine
