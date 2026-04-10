package engine

/*
#cgo CFLAGS: -I${SRCDIR}/../../libsentinel/include
#cgo LDFLAGS: -L${SRCDIR}/../../build/libsentinel -lsentinel_core -lpcap -lbpf -lxdp -lsqlite3 -latomic -lpthread -lstdc++
#include "sentinel/capi.h"
#include <stdlib.h>

// 声明导出函数
extern void goAlertCallback(void* event, void* user_data);
extern void goStatsCallback(void* stats, void* user_data);
*/
import "C"
import (
	"fmt"
	"unsafe"
)

//export goAlertCallback
func goAlertCallback(event unsafe.Pointer, userData unsafe.Pointer) {
	ev := (*C.AlertEvent)(event)
	desc := C.GoString(ev.payload_snippet)
	fmt.Printf("\n🚨 [THREAT DETECTED] Rule ID: %d | Info: %s\n", int(ev.rule_id), desc)
}

//export goStatsCallback
func goStatsCallback(stats unsafe.Pointer, userData unsafe.Pointer) {
	st := (*C.EngineStats)(stats)
	// 使用 \r 回车符实现单行实时刷新展示吞吐量
	fmt.Printf("\r📊 [引擎状态] 吞吐量: %5d Bytes/150ms | 丢包数: %d ",
		uint64(st.current_qps), uint64(st.total_packets_dropped))
}

// Engine 封装 C 引擎句柄
type Engine struct {
	handle C.SentinelEngineHandle
}

// Rule 定义传递给 C++ 的规则结构
type Rule struct {
	ID          int
	Enabled     bool
	Protocol    string
	Pattern     string
	Level       int
	Description string
}

// NewEngineWithConfig 使用完整配置创建引擎
func NewEngineWithConfig(iface string, rulesPath string, workers int, ebpf bool) *Engine {
	cIface := C.CString(iface)
	cRules := C.CString(rulesPath)
	defer C.free(unsafe.Pointer(cIface))
	defer C.free(unsafe.Pointer(cRules))

	enableEbpf := C.uint8_t(0)
	if ebpf {
		enableEbpf = C.uint8_t(1)
	}

	conf := C.SentinelConfig{
		interface_name:     cIface,
		num_worker_threads: C.uint32_t(workers),
		ring_buffer_size:   65536,
		enable_ebpf:        enableEbpf,
		rules_path:         cRules,
	}

	handle := C.sentinel_engine_create(&conf)

	C.sentinel_engine_set_callbacks(
		handle,
		(C.OnAlertCallback)(C.goAlertCallback),
		(C.OnStatsCallback)(C.goStatsCallback),
		nil,
	)
	return &Engine{handle: handle}
}

// NewEngine 保持向后兼容的简便调用（默认线程数4，eBPF关闭）
func NewEngine(iface string, rulesPath string) *Engine {
	return NewEngineWithConfig(iface, rulesPath, 4, false)
}

// AddRule 向引擎添加一条检测规则
func (e *Engine) AddRule(r Rule) {
	cProtocol := C.CString(r.Protocol)
	cPattern := C.CString(r.Pattern)
	cDesc := C.CString(r.Description)
	defer C.free(unsafe.Pointer(cProtocol))
	defer C.free(unsafe.Pointer(cPattern))
	defer C.free(unsafe.Pointer(cDesc))

	enabled := C.uint8_t(0)
	if r.Enabled {
		enabled = C.uint8_t(1)
	}

	cRule := C.SentinelRule{
		id:          C.int32_t(r.ID),
		enabled:     enabled,
		protocol:    cProtocol,
		pattern:     cPattern,
		level:       C.int32_t(r.Level),
		description: cDesc,
	}

	C.sentinel_engine_add_rule(e.handle, &cRule)
}

// ReloadRules 通知引擎重新编译规则（AC 自动机重建）
func (e *Engine) ReloadRules() {
	C.sentinel_engine_reload_rules(e.handle)
}

// Start 启动数据面捕获与处理线程
func (e *Engine) Start() error {
	if C.sentinel_engine_start(e.handle) != 0 {
		return fmt.Errorf("C++ 引擎启动失败")
	}
	return nil
}

// Stop 停止数据面处理
func (e *Engine) Stop() {
	C.sentinel_engine_stop(e.handle)
}

// Close 销毁引擎并释放所有资源
func (e *Engine) Close() {
	C.sentinel_engine_destroy(e.handle)
}