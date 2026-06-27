// Sentinel-Flow NIDS — 共享类型与常量定义。
package components

import (
	"time"

	ui "github.com/gizak/termui/v3"
)

// ---- Logo ----
const QizhiLogo = "" +
	"    ██████╗ ██╗███████╗██╗  ██╗██╗\n" +
	"   ██╔═══██╗██║╚══███╔╝██║  ██║██║\n" +
	"   ██║   ██║██║  ███╔╝ ███████║██║\n" +
	"   ██║▄▄ ██║██║ ███╔╝  ██╔══██║██║\n" +
	"   ╚██████╔╝██║███████╗██║  ██║██║\n" +
	"    ╚══▀▀═╝ ╚═╝╚══════╝╚═╝  ╚═╝╚═╝"

// ---- 威胁等级 ----
const (
	ThreatLow = iota
	ThreatMed
	ThreatHigh
	ThreatCrit
)

var ThreatLbl = [4]string{"🟢 LOW", "🟡 MED", "🔴 HIGH", "🔴 CRIT"}

// ---- AlertRecord ----

type AlertRecord struct {
	Timestamp string
	RuleID    int
	Protocol  string
	Message   string
	Level     string
	Src       string
	Dst       string
}

// ---- Snapshot：单次渲染帧的全部数据 ----

// Snapshot 捕获渲染帧的全部动态数据，由 controller 采集后分发给各组件刷新函数。
type Snapshot struct {
	StartTime      time.Time
	Iface          string
	RuleCount      int64
	PacketsAlerted uint64
	TCPAlerts      uint64
	UDPAlerts      uint64
	HTTPAlerts     uint64
	PPS            uint64
	PPSHistory     [60]uint64
	HistIdx        int
	PeakPPS        uint64
	QueueDepth     uint64
	BufferUsage    uint64
	ThreatLvl      int
	TermW          int
}

// ---- 共享样式 ----

var (
	BorderFg = ui.ColorBlack
	SparkFg  = ui.ColorCyan
)
