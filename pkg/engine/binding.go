package engine

/*
#cgo CFLAGS: -I${SRCDIR}/../../libsentinel/include
#cgo LDFLAGS: -L${SRCDIR}/../../build/libsentinel -lsentinel_core -lpcap -lbpf -lxdp -lsqlite3 -latomic -lpthread -lstdc++
#include "sentinel/capi.h"
#include <stdlib.h>

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
	fmt.Printf("\r📊 [引擎状态] 吞吐量: %5d Bytes/150ms | 丢包数: %d ", uint64(st.current_qps), uint64(st.total_packets_dropped))
}

type Engine struct {
	handle C.SentinelEngineHandle
}

type Rule struct {
	ID          int
	Enabled     bool
	Protocol    string
	Pattern     string
	Level       int
	Description string
}

func NewEngineWithConfig(iface string, rulesPath string, threads int, enableEbpf bool, offlinePcap string, verbose bool) *Engine {
	cIface := C.CString(iface)
	cRules := C.CString(rulesPath)
	
	var cOffline *C.char
	if offlinePcap != "" {
		cOffline = C.CString(offlinePcap)
		defer C.free(unsafe.Pointer(cOffline))
	}

	defer C.free(unsafe.Pointer(cIface))
	defer C.free(unsafe.Pointer(cRules))

	cEbpf, cVerbose := C.uint8_t(0), C.uint8_t(0)
	if enableEbpf { cEbpf = C.uint8_t(1) }
	if verbose { cVerbose = C.uint8_t(1) }

	conf := C.SentinelConfig{
		interface_name:     cIface,
		num_worker_threads: C.uint32_t(threads),
		ring_buffer_size:   65536,
		enable_ebpf:        cEbpf,
		rules_path:         cRules,
		offline_pcap_path:  cOffline,
		verbose:            cVerbose,
	}

	handle := C.sentinel_engine_create(&conf)
	C.sentinel_engine_set_callbacks(handle, (C.OnAlertCallback)(C.goAlertCallback), (C.OnStatsCallback)(C.goStatsCallback), nil)
	return &Engine{handle: handle}
}

func (e *Engine) ClearRules() { C.sentinel_engine_clear_rules(e.handle) }
func (e *Engine) ReloadRules() { C.sentinel_engine_reload_rules(e.handle) }
func (e *Engine) Start() error {
	errCode := C.sentinel_engine_start(e.handle)
	if errCode != 0 {
		return fmt.Errorf("C++ 引擎底层驱动启动失败, 错误码: %d", int(errCode))
	}
	return nil
}
func (e *Engine) Stop()  { C.sentinel_engine_stop(e.handle) }
func (e *Engine) Close() { C.sentinel_engine_destroy(e.handle) }

func (e *Engine) AddRule(r Rule) {
	cProtocol, cPattern, cDesc := C.CString(r.Protocol), C.CString(r.Pattern), C.CString(r.Description)
	defer C.free(unsafe.Pointer(cProtocol)); defer C.free(unsafe.Pointer(cPattern)); defer C.free(unsafe.Pointer(cDesc))

	enabled := C.uint8_t(0)
	if r.Enabled { enabled = C.uint8_t(1) }

	cRule := C.SentinelRule{
		id: C.int32_t(r.ID), enabled: enabled, protocol: cProtocol, pattern: cPattern,
		level: C.int32_t(r.Level), description: cDesc,
	}
	C.sentinel_engine_add_rule(e.handle, &cRule)
}
