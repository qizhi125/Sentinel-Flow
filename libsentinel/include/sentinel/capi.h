#ifndef SENTINEL_CAPI_H
#define SENTINEL_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

// 不透明引擎句柄
typedef void* sentinel_engine_t;

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

// 设置告警回调。当检测到威胁时，C++ 侧调用此函数指针。
// user_data 为透传的上下文指针。
typedef void (*sentinel_alert_callback_t)(int rule_id, const char* payload_snippet, void* user_data);

void sentinel_engine_set_alert_callback(sentinel_engine_t engine,
                                        sentinel_alert_callback_t callback,
                                        void* user_data);

#ifdef __cplusplus
}
#endif

#endif // SENTINEL_CAPI_H
