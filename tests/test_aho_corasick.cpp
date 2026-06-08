#include <gtest/gtest.h>
#include "sentinel/engine/match/AhoCorasick.h"

#include <string>
#include <vector>

using namespace sentinel::engine;

// ---- 单模式匹配 ----
TEST(AhoCorasickTest, SinglePattern) {
    AhoCorasick ac;
    ac.insert("test", 1001);
    ac.build();

    std::string const data = "this is a test string";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 1001);
}

// ---- 无匹配返回空 ----
TEST(AhoCorasickTest, NoMatch) {
    AhoCorasick ac;
    ac.insert("attack", 2001);
    ac.build();

    std::string const data = "benign traffic";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_FALSE(ac.match(payload, matches));
    EXPECT_TRUE(matches.empty());
}

// ---- 多模式共享前缀 ----
TEST(AhoCorasickTest, SharedPrefix) {
    AhoCorasick ac;
    ac.insert("abc", 1);
    ac.insert("abcd", 2);
    ac.insert("abce", 3);
    ac.build();

    // 匹配 "abcd" → 应命中 abc(1) + abcd(2)
    std::string const data = "abcd";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    EXPECT_EQ(matches.size(), 2u);

    // 验证包含两条规则
    bool has_1 = false, has_2 = false;
    for (int32_t id : matches) {
        if (id == 1) has_1 = true;
        if (id == 2) has_2 = true;
    }
    EXPECT_TRUE(has_1);
    EXPECT_TRUE(has_2);
}

// ---- 大小写不敏感 ----
TEST(AhoCorasickTest, CaseInsensitive) {
    AhoCorasick ac;
    ac.insert("TeSt", 1001);
    ac.build();

    std::string const data = "this is a TEST match";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 1001);
}

// ---- 空模式忽略 ----
TEST(AhoCorasickTest, EmptyPattern) {
    AhoCorasick ac;
    ac.insert("", 1001);    // 应被忽略
    ac.insert("valid", 2001);
    ac.build();

    std::string const data = "valid";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0], 2001); // 仅命中 "valid"
}

// ---- 多模式重叠命中 ----
TEST(AhoCorasickTest, MultipleMatchesInPayload) {
    AhoCorasick ac;
    ac.insert("abc", 10);
    ac.insert("def", 20);
    ac.insert("ghi", 30);
    ac.build();

    std::string const data = "abc_def_ghi";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    EXPECT_EQ(matches.size(), 3u); // 全部命中
}

// ---- 规则后缀包含关系 ----
TEST(AhoCorasickTest, SuffixMatch) {
    AhoCorasick ac;
    ac.insert("hello", 1);
    ac.insert("ello", 2);  // "hello" 的后缀
    ac.build();

    std::string const data = "hello";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    std::vector<int32_t> matches;
    EXPECT_TRUE(ac.match(payload, matches));
    // 应命中两条规则（"hello" 包含 "ello" 作为后缀）
    EXPECT_GE(matches.size(), 2u);
}

// ---- thread_local 便捷接口 ----
TEST(AhoCorasickTest, ThreadLocalMatch) {
    AhoCorasick ac;
    ac.insert("findme", 42);
    ac.build();

    std::string const data = "try to findme here";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};

    auto const* result = ac.match(payload);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0], 42);

    // 无匹配时返回 nullptr
    std::string const no_match = "nothing here";
    std::span<const uint8_t> const payload2{
        reinterpret_cast<const uint8_t*>(no_match.data()), no_match.size()};
    EXPECT_EQ(ac.match(payload2), nullptr);
}

// ---- 未 build 时的行为（仅验证不崩溃） ----
TEST(AhoCorasickTest, NotBuilt) {
    AhoCorasick ac;
    ac.insert("test", 1);
    EXPECT_FALSE(ac.is_built());

    // 未 build 时匹配行为取决于实现，此处只验证不崩溃
    std::string const data = "test";
    std::span<const uint8_t> const payload{
        reinterpret_cast<const uint8_t*>(data.data()), data.size()};
    std::vector<int32_t> matches;
    // 未 build 时跃迁表不完整，match 可能不正确但不应崩溃
    EXPECT_FALSE(ac.match(payload, matches));
    SUCCEED();
}
