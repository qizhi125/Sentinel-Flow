package engine

/*
#cgo CFLAGS: -I${SRCDIR}/../../libsentinel/include
#cgo LDFLAGS: -L${SRCDIR}/../../cmake-build-debug/lib -lsentinel_core -lpcap -lstdc++ -lsqlite3

#include "sentinel/capi.h"
#include <stdlib.h>
*/
import "C"
import (
	"sync"
	"unsafe"
)

// ---- 告警回调路由 ----

type alertCallback func(ruleID int, payloadSnippet string)

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

//export goAlertCallback
func goAlertCallback(ruleID C.int, payloadSnippet *C.char, userData unsafe.Pointer) {
	cb := lookupAlertCallback(userData)
	if cb != nil {
		cb(int(ruleID), C.GoString(payloadSnippet))
	}
}
