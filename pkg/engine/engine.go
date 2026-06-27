package engine

/*
#include "sentinel/capi.h"
#include <stdlib.h>

extern void goAlertCallback(const struct sentinel_alert_event_t* event, void* user_data);
*/
import "C"
import (
	"fmt"
	"sync"
	"unsafe"
)

// Engine 封装 C sentinel_engine_t 句柄。
// 方法非线程安全——调用方负责串行化。
type Engine struct {
	handle C.sentinel_engine_t
}

var engineCreateMu sync.Mutex

// New 创建并配置一个 Sentinel 引擎实例。
func New() (*Engine, error) {
	engineCreateMu.Lock()
	defer engineCreateMu.Unlock()

	handle := C.sentinel_engine_create()
	if handle == nil {
		return nil, fmt.Errorf("sentinel_engine_create 返回 NULL")
	}

	eng := &Engine{handle: handle}

	// 注册告警回调路由
	registerAlertCallback(handle, func(event AlertEvent) {})

	// 设置 C 侧回调
	C.sentinel_engine_set_alert_callback(
		handle,
		(C.sentinel_alert_callback_t)(C.goAlertCallback),
		unsafe.Pointer(handle),
	)

	return eng, nil
}

// Close 销毁引擎并释放所有资源。
func (e *Engine) Close() {
	if e.handle == nil {
		return
	}
	unregisterAlertCallback(e.handle)
	C.sentinel_engine_destroy(e.handle)
	e.handle = nil
}

// Start 启动捕获驱动和管线线程。
func (e *Engine) Start(device string) error {
	cDevice := C.CString(device)
	defer C.free(unsafe.Pointer(cDevice))

	rc := C.sentinel_engine_start(e.handle, cDevice)
	if rc != 0 {
		return fmt.Errorf("sentinel_engine_start 失败, 错误码: %d", int(rc))
	}
	return nil
}

// Stop 停止引擎。幂等。
func (e *Engine) Stop() {
	if e.handle == nil {
		return
	}
	C.sentinel_engine_stop(e.handle)
}

// AddRule 添加一条检测规则。需随后调用 BuildMatcher 生效。
func (e *Engine) AddRule(pattern string, id int) error {
	cPattern := C.CString(pattern)
	defer C.free(unsafe.Pointer(cPattern))

	rc := C.sentinel_engine_add_rule(e.handle, cPattern, C.int(id))
	if rc != 0 {
		return fmt.Errorf("sentinel_engine_add_rule 失败, 错误码: %d", int(rc))
	}
	return nil
}

// ClearRules 清空所有待构建规则。
func (e *Engine) ClearRules() {
	C.sentinel_engine_clear_rules(e.handle)
}

// BuildMatcher 构建 AC 自动机。必须在所有 AddRule 之后调用。
func (e *Engine) BuildMatcher() error {
	rc := C.sentinel_engine_build_matcher(e.handle)
	if rc != 0 {
		return fmt.Errorf("sentinel_engine_build_matcher 失败, 错误码: %d", int(rc))
	}
	return nil
}

// RuleCount 返回待构建规则数量。
func (e *Engine) RuleCount() int {
	return int(C.sentinel_engine_rule_count(e.handle))
}

// AlertCallback 是告警回调签名。
type AlertCallback = alertCallback

// SetAlertCallback 替换当前引擎的告警回调。
// 线程安全 — 可在引擎运行时调用。
func (e *Engine) SetAlertCallback(cb AlertCallback) {
	registerAlertCallback(e.handle, cb)
}

// SetStatsCallback 设置遥测统计回调。
// 回调在 C++ 统计线程上下文执行（非主线程），调用方负责线程安全。
func (e *Engine) SetStatsCallback(cb statsCallback) {
	registerStatsCallback(e.handle, cb)

	// 注册 C 侧回调（仅首次）
	C.sentinel_engine_set_stats_callback(
		e.handle,
		(C.sentinel_stats_callback_t)(C.goStatsCallback),
		unsafe.Pointer(e.handle),
	)
}
