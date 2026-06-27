// Sentinel-Flow NIDS — 卡片组件（中枢三卡片 + KPI 四卡片 + Sparkline 流量图）。
package components

import (
	"fmt"
	"strings"
	"time"

	ui "github.com/gizak/termui/v3"
	"github.com/gizak/termui/v3/widgets"
)

// ---- 中枢卡片构造函数 ----

// NewInfoCard 创建运行信息卡片（显示 uptime、接口、规则数、状态）。
func NewInfoCard() *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Border = true
	p.BorderStyle = ui.NewStyle(ui.ColorGreen)
	p.Title = " 运行信息 "
	p.TitleStyle = ui.NewStyle(ui.ColorGreen)
	return p
}

// NewCoreCard 创建威胁中枢卡片（显示威胁等级、告警总数、速率、队列深度）。
func NewCoreCard() *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Border = true
	p.BorderStyle = ui.NewStyle(ui.ColorRed)
	p.Title = " 威胁中枢 "
	p.TitleStyle = ui.NewStyle(ui.ColorRed)
	return p
}

// NewTrafficCard 创建协议分布卡片（显示 Sparkline + TCP/UDP/HTTP 计数）。
func NewTrafficCard() *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Border = true
	p.BorderStyle = ui.NewStyle(ui.ColorCyan)
	p.Title = " 协议分布 "
	p.TitleStyle = ui.NewStyle(ui.ColorCyan)
	return p
}

// NewKPICard 创建一个带标题的通用 KPI 卡片。
func NewKPICard(title string) *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Border = true
	p.BorderStyle = ui.NewStyle(BorderFg)
	p.Title = " " + title + " "
	p.TitleStyle = ui.NewStyle(ui.ColorWhite)
	return p
}

// ---- Sparkline 流量图构造函数 ----

// NewSparklineGroup 创建流量趋势 SparklineGroup 控件。
func NewSparklineGroup() *widgets.SparklineGroup {
	sl := widgets.NewSparkline()
	sl.LineColor = SparkFg
	sl.Title = "Traffic (60s)"
	sl.TitleStyle = ui.NewStyle(ui.ColorWhite)
	sl.MaxHeight = 3
	sg := widgets.NewSparklineGroup(sl)
	sg.Border = true
	sg.BorderStyle = ui.NewStyle(BorderFg)
	sg.Title = " 流量趋势 "
	sg.TitleStyle = ui.NewStyle(ui.ColorCyan)
	return sg
}

// ---- 刷新函数：按控件粒度拆分，支持差异渲染 ----

// RefreshInfoCard 刷新运行信息卡片。
func RefreshInfoCard(p *widgets.Paragraph, snap Snapshot) {
	p.Text = fmt.Sprintf(
		"运行: %s  |  接口: %s\n规则: %s     |  状态: 🟢 引擎运行中",
		PadDur(time.Since(snap.StartTime), 12),
		snap.Iface,
		PadNum(uint64(snap.RuleCount), 5),
	)
}

// RefreshCoreCard 刷新威胁中枢卡片。
func RefreshCoreCard(p *widgets.Paragraph, snap Snapshot) {
	p.Text = fmt.Sprintf(
		"威胁: %s  |  总数: %s\n速率: %s /s  |  队列: %s",
		ThreatLbl[snap.ThreatLvl],
		PadNum(snap.PacketsAlerted, 6),
		PadNum(snap.PPS, 6),
		PadNum(snap.QueueDepth, 4),
	)
}

// RefreshTrafficCard 刷新协议分布卡片。
func RefreshTrafficCard(p *widgets.Paragraph, snap Snapshot) {
	h := windowFromHistory(snap.PPSHistory, snap.HistIdx, 40)
	p.Text = fmt.Sprintf("%s\nTCP:%s | UDP:%s | HTTP:%s",
		Sparkline(h, snap.PeakPPS),
		PadNum(snap.TCPAlerts, 4),
		PadNum(snap.UDPAlerts, 4),
		PadNum(snap.HTTPAlerts, 4),
	)
}

// RefreshThroughputCard 刷新 Throughput KPI 卡片。
func RefreshThroughputCard(p *widgets.Paragraph, snap Snapshot) {
	h := windowFromHistory(snap.PPSHistory, snap.HistIdx, 20)
	p.Text = fmt.Sprintf("%d pps\n%s", snap.PPS, Sparkline(h, snap.PeakPPS))
}

// RefreshQueueCard 刷新 Queue Depth KPI 卡片。
func RefreshQueueCard(p *widgets.Paragraph, snap Snapshot) {
	bw := snap.TermW/4 - 8
	if bw < 4 {
		bw = 4
	}
	q := snap.QueueDepth
	pct := q * 100 / 2048
	f := int(pct) * bw / 100
	p.Text = fmt.Sprintf("%d pkts\n%s",
		q, strings.Repeat("#", f)+strings.Repeat("·", bw-f))
}

// RefreshBufferCard 刷新 Buffer Usage KPI 卡片。
func RefreshBufferCard(p *widgets.Paragraph, snap Snapshot) {
	bw := snap.TermW/4 - 8
	if bw < 4 {
		bw = 4
	}
	bu := snap.BufferUsage
	f := int(bu) * bw / 100
	p.Text = fmt.Sprintf("%d%%\n%s",
		bu, strings.Repeat("#", f)+strings.Repeat("·", bw-f))
}

// RefreshPeakCard 刷新 Peak Traffic KPI 卡片。
func RefreshPeakCard(p *widgets.Paragraph, snap Snapshot) {
	h := windowFromHistory(snap.PPSHistory, snap.HistIdx, 20)
	p.Text = fmt.Sprintf("peak %d/s\n%s",
		snap.PeakPPS, Sparkline(h, snap.PeakPPS))
}

// RefreshSparklineGroup 刷新流量趋势图数据。
func RefreshSparklineGroup(sg *widgets.SparklineGroup, snap Snapshot) {
	gw := snap.TermW - 6
	if gw > 60 {
		gw = 60
	}
	if gw < 20 {
		gw = 20
	}
	graphData := make([]float64, 0, gw)
	for i := 0; i < gw && i < 60; i++ {
		graphData = append(graphData,
			float64(snap.PPSHistory[(snap.HistIdx-gw+i+60)%60]))
	}
	if len(sg.Sparklines) > 0 {
		sg.Sparklines[0].Data = graphData
		sg.Sparklines[0].MaxVal = float64(snap.PeakPPS)
	}
	sg.Title = fmt.Sprintf(" 流量趋势 (60s) - peak %d/s ", snap.PeakPPS)
}

// windowFromHistory 从环形历史缓冲区中提取最近 n 个值。
func windowFromHistory(hist [60]uint64, idx, n int) []uint64 {
	out := make([]uint64, n)
	for i := 0; i < n && i < 60; i++ {
		out[i] = hist[(idx-n+i+60)%60]
	}
	return out
}
