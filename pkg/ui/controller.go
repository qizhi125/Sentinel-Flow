// Sentinel-Flow NIDS — 差异渲染控制器。
// 取代 100ms 全量刷新模型：事件驱动 + 脏标记 + 值比较，仅对变更组件调用 ui.Render。
package ui

import (
	"log"
	"time"

	ui "github.com/gizak/termui/v3"

	"github.com/qizhi125/Sentinel-Flow/pkg/ui/components"
)

// renderMask 表示需渲染的组件位掩码。
// 每位对应一个独立控件，支持按位合并与测试。
type renderMask uint16

const (
	maskInfo      renderMask = 1 << iota // 运行信息卡片
	maskCore                             // 威胁中枢卡片
	maskTraffic                          // 协议分布卡片
	maskTput                             // Throughput KPI 卡片
	maskQueue                            // Queue Depth KPI 卡片
	maskBuffer                           // Buffer Usage KPI 卡片
	maskPeak                             // Peak Traffic KPI 卡片
	maskSparkline                        // 流量趋势 Sparkline 组
	maskTable                            // 告警表格

	// 聚合掩码
	maskHub = maskInfo | maskCore | maskTraffic
	maskKPI = maskTput | maskQueue | maskBuffer | maskPeak
	maskAll = maskHub | maskKPI | maskSparkline | maskTable
)

// runLoop 启动 termui 事件/差异渲染循环（阻塞）。
func (d *Dashboard) runLoop() error {
	// 心跳探针（5s，确认主循环存活）
	heartbeatTicker := time.NewTicker(5 * time.Second)
	defer heartbeatTicker.Stop()

	// 首次全量渲染：刷新文本 + Grid 分配子控件坐标 + 绘制全部控件
	d.initStatsBaseline()
	snap := d.captureSnapshot()
	// renderFrameTextOnly: 仅更新控件文本，不调用 ui.Render（控件此时 rect=0x0）
	d.renderFrameTextOnly(snap, maskAll)
	// Grid.Draw() 是子控件 SetRect(x,y,w,h) 的唯一调用点 — 必须在此处绘制整棵控件树
	safeRender("grid-init", d.grid)
	d.prevSnap = snap
	log.Printf("[main-loop] 初始全量渲染完成 mask=%016b", maskAll)

	uiEvents := ui.PollEvents()

	for {
		select {
		case e := <-uiEvents:
			switch e.ID {
			case "q", "<C-c>":
				log.Printf("[main-loop] 退出事件 e.ID=%q", e.ID)
				return nil
			case "<Resize>":
				log.Printf("[main-loop] 窗口尺寸变更")
				d.handleResize()
			default:
				d.handleKeyboard(e.ID)
			}
		case <-d.statsTick:
			// 统计轮询来自后台 StatsLoop goroutine；
			// updateStats + diffAndRender 仍在主 goroutine 执行，保证字段线程安全
			d.updateStats()
			d.diffAndRender(0)
		case mask := <-d.renderCh:
			// 合并数据驱动标记与值比较结果
			log.Printf("[main-loop] 收到渲染请求 mask=%016b", mask)
			d.diffAndRender(mask)
		case <-heartbeatTicker.C:
			log.Printf("[main-loop] 心跳 — alive PPS=%d alerts=%d threatLvl=%d",
				d.pps, d.packetsAlerted.Load(), d.threatLvl)
		case <-d.quit:
			log.Printf("[main-loop] 收到 quit 信号，安全退出")
			return nil
		}
	}
}

// initStatsBaseline 初始化统计基线，避免首次 PPS 计算产生尖峰。
func (d *Dashboard) initStatsBaseline() {
	d.prevAlerted = d.packetsAlerted.Load()
	d.pps = 0
}

// updateStats 计算每秒速率、威胁等级等派生统计量。
// 由 1s statsTicker 驱动。
func (d *Dashboard) updateStats() {
	cur := d.packetsAlerted.Load()
	d.pps = cur - d.prevAlerted // 1s 间隔，直接差值
	d.prevAlerted = cur
	d.ppsHistory[d.histIdx] = d.pps
	d.histIdx = (d.histIdx + 1) % 60
	if d.pps > d.peakPps {
		d.peakPps = d.pps
	}
	// 真实引擎遥测（由 C++ 统计线程写入，主 goroutine 安全读取）
	d.queueDepth = d.engineQueueDepth.Load()
	d.bufferUsage = d.engineDBBufUsage.Load()
	switch {
	case d.pps > 100:
		d.threatLvl = components.ThreatCrit
	case d.pps > 50:
		d.threatLvl = components.ThreatHigh
	case d.pps > 10:
		d.threatLvl = components.ThreatMed
	default:
		d.threatLvl = components.ThreatLow
	}
}

// captureSnapshot 从 Dashboard 状态采集一份不可变快照。
func (d *Dashboard) captureSnapshot() components.Snapshot {
	termW, _ := ui.TerminalDimensions()
	return components.Snapshot{
		StartTime:      d.startTime,
		Iface:          d.iface,
		RuleCount:      d.ruleCount.Load(),
		PacketsAlerted: d.packetsAlerted.Load(),
		TCPAlerts:      d.tcpAlerts.Load(),
		UDPAlerts:      d.udpAlerts.Load(),
		HTTPAlerts:     d.httpAlerts.Load(),
		PPS:            d.pps,
		PPSHistory:     d.ppsHistory,
		HistIdx:        d.histIdx,
		PeakPPS:        d.peakPps,
		QueueDepth:     d.queueDepth,
		BufferUsage:    d.bufferUsage,
		ThreatLvl:      d.threatLvl,
		TermW:          termW,
	}
}

// diffAndRender 采集快照并逐字段比较，仅渲染变更组件。force 用于强制刷新组件（如告警到达）。
func (d *Dashboard) diffAndRender(force renderMask) {
	snap := d.captureSnapshot()
	mask := force | d.computeDiff(snap)
	if mask == 0 {
		log.Printf("[diff] 无变更，跳过渲染")
		return
	}
	log.Printf("[diff] 渲染触发 force=%09b diff=%09b combined=%09b", force, mask&^force, mask)
	d.renderFrame(snap, mask)
	d.prevSnap = snap
}

// computeDiff 逐字段比较新旧快照，返回脏组件掩码。
// 每个组件仅检查其实际依赖的 Snapshot 字段。
func (d *Dashboard) computeDiff(snap components.Snapshot) renderMask {
	prev := d.prevSnap
	var mask renderMask

	// 信息卡片在 statsTicker 触发时总是刷新（uptime 显示）
	mask |= maskInfo

	// 威胁中枢：依赖威胁等级、告警总数、PPS、队列深度
	if snap.ThreatLvl != prev.ThreatLvl ||
		snap.PacketsAlerted != prev.PacketsAlerted ||
		snap.PPS != prev.PPS ||
		snap.QueueDepth != prev.QueueDepth {
		mask |= maskCore
	}

	// 协议分布：依赖协议计数、直方图索引、峰值 PPS
	if snap.TCPAlerts != prev.TCPAlerts ||
		snap.UDPAlerts != prev.UDPAlerts ||
		snap.HTTPAlerts != prev.HTTPAlerts ||
		snap.HistIdx != prev.HistIdx ||
		snap.PeakPPS != prev.PeakPPS {
		mask |= maskTraffic
	}

	if snap.PPS != prev.PPS || snap.HistIdx != prev.HistIdx || snap.PeakPPS != prev.PeakPPS {
		mask |= maskTput
	}
	if snap.QueueDepth != prev.QueueDepth || snap.TermW != prev.TermW {
		mask |= maskQueue
	}
	if snap.BufferUsage != prev.BufferUsage || snap.TermW != prev.TermW {
		mask |= maskBuffer
	}
	if snap.HistIdx != prev.HistIdx || snap.PeakPPS != prev.PeakPPS {
		mask |= maskPeak
	}

	if snap.HistIdx != prev.HistIdx || snap.PeakPPS != prev.PeakPPS || snap.TermW != prev.TermW {
		mask |= maskSparkline
	}

	// 告警表格：TermW 变更时重算列宽（resize 场景无需等待告警到达）
	if snap.TermW != prev.TermW {
		mask |= maskTable
	}

	return mask
}

// safeRender 用 recover 包裹 ui.Render 调用，防止 termui 内部 panic 导致主循环崩溃。
// 任何 panic 均被捕获并记录到遥测日志。
func safeRender(label string, drawables ...ui.Drawable) {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("[render] PANIC 恢复 label=%s panic=%v", label, r)
		}
	}()
	ui.Render(drawables...)
}

// renderFrameTextOnly 刷新控件文本，不调用 ui.Render（由 Grid.Draw 统一分配坐标并绘制）。
func (d *Dashboard) renderFrameTextOnly(snap components.Snapshot, mask renderMask) {
	if mask&maskInfo != 0 {
		components.RefreshInfoCard(d.infoPara, snap)
	}
	if mask&maskCore != 0 {
		components.RefreshCoreCard(d.corePara, snap)
	}
	if mask&maskTraffic != 0 {
		components.RefreshTrafficCard(d.trafPara, snap)
	}
	if mask&maskTput != 0 {
		components.RefreshThroughputCard(d.tputPara, snap)
	}
	if mask&maskQueue != 0 {
		components.RefreshQueueCard(d.queuePara, snap)
	}
	if mask&maskBuffer != 0 {
		components.RefreshBufferCard(d.bufPara, snap)
	}
	if mask&maskPeak != 0 {
		components.RefreshPeakCard(d.peakPara, snap)
	}
	if mask&maskSparkline != 0 {
		components.RefreshSparklineGroup(d.graphGroup, snap)
	}
	if mask&maskTable != 0 {
		alertsCopy := d.copyAlerts()
		components.RefreshAlertTable(d.alertTable, alertsCopy, snap.TermW)
	}
}

// renderFrame 根据掩码选择性刷新组件数据并调用 ui.Render（差异渲染路径）。
func (d *Dashboard) renderFrame(snap components.Snapshot, mask renderMask) {
	log.Printf("[render] 开始渲染 mask=%09b", mask)

	if mask&maskInfo != 0 {
		components.RefreshInfoCard(d.infoPara, snap)
		safeRender("info", d.infoPara)
	}
	if mask&maskCore != 0 {
		components.RefreshCoreCard(d.corePara, snap)
		safeRender("core", d.corePara)
	}
	if mask&maskTraffic != 0 {
		components.RefreshTrafficCard(d.trafPara, snap)
		safeRender("traffic", d.trafPara)
	}

	if mask&maskTput != 0 {
		components.RefreshThroughputCard(d.tputPara, snap)
		safeRender("tput", d.tputPara)
	}
	if mask&maskQueue != 0 {
		components.RefreshQueueCard(d.queuePara, snap)
		safeRender("queue", d.queuePara)
	}
	if mask&maskBuffer != 0 {
		components.RefreshBufferCard(d.bufPara, snap)
		safeRender("buffer", d.bufPara)
	}
	if mask&maskPeak != 0 {
		components.RefreshPeakCard(d.peakPara, snap)
		safeRender("peak", d.peakPara)
	}

	if mask&maskSparkline != 0 {
		components.RefreshSparklineGroup(d.graphGroup, snap)
		safeRender("sparkline", d.graphGroup)
	}

	// 告警表格（控制器持锁复制数据，组件执行纯格式化）
	if mask&maskTable != 0 {
		alertsCopy := d.copyAlerts()
		components.RefreshAlertTable(d.alertTable, alertsCopy, snap.TermW)
		safeRender("table", d.alertTable)
	}

	log.Printf("[render] 渲染完成 mask=%09b", mask)
}

// handleResize 响应终端尺寸变更，重新计算所有组件的布局并全量渲染。
func (d *Dashboard) handleResize() {
	// 重建网格布局（复用已创建的控件）
	d.rebuildLayout()

	// 全量渲染
	snap := d.captureSnapshot()
	d.renderFrameTextOnly(snap, maskAll)
	safeRender("grid-resize", d.grid)
	d.prevSnap = snap
}

// handleKeyboard 处理键盘事件，按需更新受影响的控件。
func (d *Dashboard) handleKeyboard(id string) {
	switch id {
	case "<Resize>":
		// 由主循环 case 分支处理，此处不重复
	case "p", "P":
		// 暂停/恢复刷新切换（预留）
	default:
		// 未绑定按键，静默忽略
	}
}

// copyAlerts 在持有锁的情况下复制告警切片，供组件刷新使用。
func (d *Dashboard) copyAlerts() []components.AlertRecord {
	d.mu.Lock()
	defer d.mu.Unlock()
	out := make([]components.AlertRecord, len(d.alerts))
	copy(out, d.alerts)
	return out
}
