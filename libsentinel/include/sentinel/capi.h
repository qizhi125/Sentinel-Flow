#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 不透明引擎句柄
typedef void* sentinel_engine_t;

// ---- 引擎遥测 ----

// 引擎实时遥测统计量。
// 所有字段由后台统计线程定期采样，约 1Hz 频率触发回调。
struct sentinel_engine_stats_t {
    uint64_t packets_received;   // 捕获驱动接收的数据包总数
    uint64_t packets_dropped;    // SPSC 队列溢出导致丢弃的数据包总数
    uint32_t queue_depth;        // SPSC 队列当前深度（近似值）
    uint32_t db_buffer_usage;    // 数据库前端缓冲区当前条目数（近似值）
    bool     has_fatal_error;    // 管线消费者线程是否已异常退出（Go 侧应触发安全关机）
};

// ---- 引擎生命周期 ----

// 创建引擎实例。成功返回非空句柄，失败返回 NULL。
sentinel_engine_t sentinel_engine_create(void);

// 销毁引擎实例，释放所有资源。销毁后句柄失效。
void sentinel_engine_destroy(sentinel_engine_t engine);

// 启动引擎。device 为网卡名称（如 "eth0"、"lo"）或离线 pcap 路径。
// 返回 0 表示成功，负数表示错误码。
int sentinel_engine_start(sentinel_engine_t engine, const char* device);

// 停止引擎。阻塞直到捕获和管线线程退出。幂等。
void sentinel_engine_stop(sentinel_engine_t engine);

// ---- 规则管理 ----

// 添加一条检测规则。pattern 为匹配模式串（ASCII），rule_id 为关联的规则 ID。
// 返回 0 表示成功，负数表示错误码。
int sentinel_engine_add_rule(sentinel_engine_t engine, const char* pattern, int rule_id);

// 清空所有已添加的规则。
void sentinel_engine_clear_rules(sentinel_engine_t engine);

// 构建 AC 自动机。必须在所有 add_rule 之后、首次 start 之前调用。
// 返回 0 表示成功，负数表示错误码。
int sentinel_engine_build_matcher(sentinel_engine_t engine);

// 获取已添加的规则数量。
int sentinel_engine_rule_count(sentinel_engine_t engine);

// 告警事件结构体 — 完整五元组 + 时间戳 + 载荷快照。
// payload_snippet 指向栈临时缓冲区，仅在回调执行期间有效。
struct sentinel_alert_event_t {
    int rule_id;
    uint32_t src_ip;          // 网络字节序 IPv4 地址（Go 侧用 net.IP 转换）
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;        // IANA IP 协议号（IPPROTO_TCP=6, IPPROTO_UDP=17, IPPROTO_ICMP=1）
    int64_t  timestamp_ns;    // 内核纳秒时间戳
    const char* payload_snippet; // 应用层载荷快照（栈分配，仅回调期间有效）
};

// 设置告警回调。当检测到威胁时，C++ 侧传递 sentinel_alert_event_t 指针。
// event 指针和其 payload_snippet 仅在回调执行期间有效，调用方不得保存。
// user_data 为透传的上下文指针。
typedef void (*sentinel_alert_callback_t)(const struct sentinel_alert_event_t* event, void* user_data);

void sentinel_engine_set_alert_callback(sentinel_engine_t engine,
                                        sentinel_alert_callback_t callback,
                                        void* user_data);

// 统计遥测回调。由引擎内部统计线程以约 1 Hz 频率周期调用。
// user_data 为透传的上下文指针。回调在统计线程上下文执行，
// 调用方负责线程安全。
typedef void (*sentinel_stats_callback_t)(const struct sentinel_engine_stats_t* stats, void* user_data);

void sentinel_engine_set_stats_callback(sentinel_engine_t engine,
                                        sentinel_stats_callback_t callback,
                                        void* user_data);

#ifdef __cplusplus
}
#endif

