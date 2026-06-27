package engine

/*
#cgo CFLAGS: -I${SRCDIR}/../../libsentinel/include
#cgo LDFLAGS: -L${SRCDIR}/../../cmake-build-debug/lib -lsentinel_core -lpcap -lstdc++ -lsqlite3

#include "sentinel/capi.h"
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"sync"
	"unsafe"
)

// AlertEvent 表示完整的五元组告警事件。
type AlertEvent struct {
	RuleID         int
	SrcIP          string // 点分十进制 IPv4，如 "192.168.1.1"
	DstIP          string
	SrcPort        uint16
	DstPort        uint16
	Protocol       string // "TCP" / "UDP" / "ICMP" / …
	ProtocolIANA   uint8  // IANA IP 协议号（IPPROTO_TCP=6, IPPROTO_UDP=17）
	TimestampNs    int64
	PayloadSnippet string
}

// ---- 告警回调路由 ----

type alertCallback func(event AlertEvent)

var (
	alertRegistry   = make(map[unsafe.Pointer]alertCallback)
	alertRegistryMu sync.RWMutex
)

func registerAlertCallback(handle C.sentinel_engine_t, cb alertCallback) {
	alertRegistryMu.Lock()
	defer alertRegistryMu.Unlock()
	alertRegistry[unsafe.Pointer(handle)] = cb
}

func unregisterAlertCallback(handle C.sentinel_engine_t) {
	alertRegistryMu.Lock()
	defer alertRegistryMu.Unlock()
	delete(alertRegistry, unsafe.Pointer(handle))
}

func lookupAlertCallback(handle unsafe.Pointer) alertCallback {
	alertRegistryMu.RLock()
	defer alertRegistryMu.RUnlock()
	return alertRegistry[handle]
}

// uint32ToIPv4 将网络字节序 uint32 转换为点分十进制 IPv4 字符串。
func uint32ToIPv4(ip uint32) string {
	return fmt.Sprintf("%d.%d.%d.%d", byte(ip>>24), byte(ip>>16), byte(ip>>8), byte(ip))
}

// ianaProtocolName 将 IANA 协议号映射为人类可读名称。
func ianaProtocolName(proto uint8) string {
	switch proto {
	case 1:
		return "ICMP"
	case 6:
		return "TCP"
	case 17:
		return "UDP"
	default:
		return fmt.Sprintf("IP(%d)", proto)
	}
}

//export goAlertCallback
func goAlertCallback(event *C.struct_sentinel_alert_event_t, userData unsafe.Pointer) {
	cb := lookupAlertCallback(userData)
	if cb != nil {
		cb(AlertEvent{
			RuleID:         int(event.rule_id),
			SrcIP:          uint32ToIPv4(uint32(event.src_ip)),
			DstIP:          uint32ToIPv4(uint32(event.dst_ip)),
			SrcPort:        uint16(event.src_port),
			DstPort:        uint16(event.dst_port),
			Protocol:       ianaProtocolName(uint8(event.protocol)),
			ProtocolIANA:   uint8(event.protocol),
			TimestampNs:    int64(event.timestamp_ns),
			PayloadSnippet: C.GoString(event.payload_snippet),
		})
	}
}

// ---- 遥测统计回调路由 ----

// EngineStats 对应 sentinel_engine_stats_t。
type EngineStats struct {
	PacketsReceived uint64
	PacketsDropped  uint64
	QueueDepth      uint32
	DBBufferUsage   uint32
	HasFatalError   bool // 管线消费者线程是否已异常退出
}

type statsCallback func(stats EngineStats)

var (
	statsRegistry   = make(map[unsafe.Pointer]statsCallback)
	statsRegistryMu sync.RWMutex
)

func registerStatsCallback(handle C.sentinel_engine_t, cb statsCallback) {
	statsRegistryMu.Lock()
	defer statsRegistryMu.Unlock()
	statsRegistry[unsafe.Pointer(handle)] = cb
}

func unregisterStatsCallback(handle C.sentinel_engine_t) {
	statsRegistryMu.Lock()
	defer statsRegistryMu.Unlock()
	delete(statsRegistry, unsafe.Pointer(handle))
}

func lookupStatsCallback(handle unsafe.Pointer) statsCallback {
	statsRegistryMu.RLock()
	defer statsRegistryMu.RUnlock()
	return statsRegistry[handle]
}

//export goStatsCallback
func goStatsCallback(stats *C.struct_sentinel_engine_stats_t, userData unsafe.Pointer) {
	cb := lookupStatsCallback(userData)
	if cb != nil {
		cb(EngineStats{
			PacketsReceived: uint64(stats.packets_received),
			PacketsDropped:  uint64(stats.packets_dropped),
			QueueDepth:      uint32(stats.queue_depth),
			DBBufferUsage:   uint32(stats.db_buffer_usage),
			HasFatalError:   bool(stats.has_fatal_error),
		})
	}
}
