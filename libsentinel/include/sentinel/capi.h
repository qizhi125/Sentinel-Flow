#ifndef SENTINEL_CAPI_H
#define SENTINEL_CAPI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SentinelConfig {
    const char* interface_name;
    uint32_t num_worker_threads;
    uint32_t ring_buffer_size;
    uint8_t enable_ebpf;
    const char* rules_path;
    const char* offline_pcap_path;
    uint8_t verbose;
} SentinelConfig;

typedef void* SentinelEngineHandle;

typedef struct SentinelRule {
    int32_t id;
    uint8_t enabled;
    const char* protocol;
    const char* pattern;
    int32_t level;
    const char* description;
} SentinelRule;

typedef struct AlertEvent {
    uint64_t timestamp_ns;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    int32_t  rule_id;
    const char* payload_snippet;
} AlertEvent;

typedef struct EngineStats {
    uint64_t total_packets_received;
    uint64_t total_packets_dropped;
    uint64_t current_qps;
    uint32_t active_flows;
} EngineStats;

typedef void (*OnAlertCallback)(const AlertEvent* event, void* user_data);
typedef void (*OnStatsCallback)(const EngineStats* stats, void* user_data);

SentinelEngineHandle sentinel_engine_create(const SentinelConfig* config);
void sentinel_engine_set_callbacks(SentinelEngineHandle handle, OnAlertCallback alert_cb, OnStatsCallback stats_cb, void* user_data);
int sentinel_engine_start(SentinelEngineHandle handle);
void sentinel_engine_stop(SentinelEngineHandle handle);
void sentinel_engine_destroy(SentinelEngineHandle handle);

void sentinel_engine_clear_rules(SentinelEngineHandle handle);
void sentinel_engine_add_rule(SentinelEngineHandle handle, const SentinelRule* rule);
int sentinel_engine_reload_rules(SentinelEngineHandle handle);

#ifdef __cplusplus
}
#endif

#endif // SENTINEL_CAPI_H
