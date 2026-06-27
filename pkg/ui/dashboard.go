// Sentinel-Flow NIDS — termui Widget/Grid 架构。
// 仪表盘入口：持有所有控件、数据源与 Grid 布局组装，委托 controller 驱动渲染循环。
package ui

import (
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	ui "github.com/gizak/termui/v3"
	"github.com/gizak/termui/v3/widgets"

	"github.com/qizhi125/Sentinel-Flow/pkg/ui/components"
)

// AlertRecord 重新导出，保持外部调用方（main.go）兼容。
type AlertRecord = components.AlertRecord

// ---- Dashboard（termui Widget/Grid 架构） ----

// Dashboard 持有全部渲染状态与 termui 控件。
// 公共 API 线程安全；渲染循环由 controller.runLoop 在 Start() 中驱动。
type Dashboard struct {
	startTime      time.Time
	packetsAlerted atomic.Uint64
	tcpAlerts      atomic.Uint64
	udpAlerts      atomic.Uint64
	httpAlerts     atomic.Uint64
	ruleCount      atomic.Int64

	mu          sync.Mutex
	alerts      []components.AlertRecord
	max         int
	prevAlerted uint64
	pps         uint64
	ppsHistory  [60]uint64
	histIdx     int
	peakPps     uint64

	queueDepth, bufferUsage uint64
	threatLvl               int

	// 真实引擎遥测（由 CGO 统计回调写入，主 goroutine 读取）
	enginePacketsRecv atomic.Uint64
	enginePacketsDrop atomic.Uint64
	engineQueueDepth  atomic.Uint64
	engineDBBufUsage  atomic.Uint64

	iface string

	// termui 控件（由 buildLayout 初始化）
	bannerPara *widgets.Paragraph
	infoPara   *widgets.Paragraph
	corePara   *widgets.Paragraph
	trafPara   *widgets.Paragraph
	tputPara   *widgets.Paragraph
	queuePara  *widgets.Paragraph
	bufPara    *widgets.Paragraph
	peakPara   *widgets.Paragraph
	graphGroup *widgets.SparklineGroup
	alertTable *widgets.Table
	footerPara *widgets.Paragraph

	// 布局根节点
	grid *ui.Grid

	// 差异渲染状态
	prevSnap  components.Snapshot // 上一帧快照，用于逐字段值比较
	renderCh  chan renderMask     // 渲染请求通道（缓冲 1，合并高频告警）
	statsTick chan struct{}       // 统计轮询触发通道（缓冲 1，由 StatsLoop 发送）

	// 生命周期
	quit chan struct{} // 内部退出信号（Quit 发送）
	done chan struct{} // 外部可观测退出信号（Start 退出时关闭）
}

// ---- 公共 API ----

// NewDashboard 创建 termui 仪表盘实例。
func NewDashboard(maxAlerts int, iface string) *Dashboard {
	return &Dashboard{
		startTime: time.Now(),
		max:       maxAlerts,
		alerts:    make([]components.AlertRecord, 0, maxAlerts),
		iface:     iface,
		quit:      make(chan struct{}, 1),
		done:      make(chan struct{}),
		renderCh:  make(chan renderMask, 1), // 缓冲 1，合并高频渲染请求
		statsTick: make(chan struct{}, 1),   // 缓冲 1，合并统计轮询
	}
}

// Start 初始化 termui 并进入事件/刷新循环（阻塞）。
// 必须在已锁定 OS 线程的主 goroutine 调用。
func (d *Dashboard) Start() error {
	// 确保 done 在退出时关闭（仅一次）
	defer func() {
		select {
		case <-d.done:
			// 已关闭
		default:
			close(d.done)
		}
	}()

	if err := ui.Init(); err != nil {
		return fmt.Errorf("termui 初始化失败: %w", err)
	}
	defer ui.Close()

	d.buildLayout()
	return d.runLoop()
}

// Quit 安全停止渲染循环。幂等，可从任意 goroutine 调用。
func (d *Dashboard) Quit() {
	select {
	case d.quit <- struct{}{}:
	default:
	}
}

// StatsLoop 在后台 goroutine 中运行，每秒向主事件循环推送统计轮询触发信号。
// 统计计算与差异渲染仍在主 goroutine 执行，保证 Dashboard 字段的线程安全。
func (d *Dashboard) StatsLoop() {
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			select {
			case d.statsTick <- struct{}{}:
			default:
				// 上一轮统计尚未消费，跳过本轮（合并）
			}
		case <-d.quit:
			return
		}
	}
}

// Done 返回一个在 Start() 退出时关闭的通道。外部 goroutine 可 select 此通道
// 以观测 UI 生命周期结束，无需轮询。
func (d *Dashboard) Done() <-chan struct{} {
	return d.done
}

// SendAlert 线程安全地推送告警数据，仅标记脏数据，实际渲染推迟到下一 tick。
func (d *Dashboard) SendAlert(r components.AlertRecord) {
	d.packetsAlerted.Add(1)
	switch r.Protocol {
	case "TCP":
		d.tcpAlerts.Add(1)
	case "UDP":
		d.udpAlerts.Add(1)
	case "HTTP":
		d.httpAlerts.Add(1)
	}
	d.mu.Lock()
	d.alerts = append([]components.AlertRecord{r}, d.alerts...)
	if len(d.alerts) > d.max {
		d.alerts = d.alerts[:d.max]
	}
	d.mu.Unlock()

	log.Printf("[alert] rule=%d proto=%s level=%s len=%d msg=%q",
		r.RuleID, r.Protocol, r.Level, len(r.Message), components.Sanitize(r.Message))

	// 非阻塞推送渲染请求：仅标记受影响的组件（告警表格 + 威胁中枢 + 协议分布）
	select {
	case d.renderCh <- maskTable | maskCore | maskTraffic:
	default:
		// 通道已满，上一请求尚未消费，当前告警自动合并批量渲染
	}
}

// UpdateRuleCount 由配置热重载回调线程安全地更新规则计数。
func (d *Dashboard) UpdateRuleCount(n int) {
	d.ruleCount.Store(int64(n))
	select {
	case d.renderCh <- maskInfo:
	default:
	}
}

func (d *Dashboard) TotalAlerts() uint64 {
	return d.packetsAlerted.Load()
}

// UpdateTelemetry 由 CGO 遥测统计回调线程安全地写入真实引擎指标。
// CGO 回调在 C++ 统计线程上下文执行（非主 goroutine），因此仅执行原子存储，
// 不操作 UI 控件。主 goroutine 在 updateStats() 中读取这些值并触发差异渲染。
func (d *Dashboard) UpdateTelemetry(packetsRecv, packetsDrop, queueDepth, dbBufUsage uint64) {
	d.enginePacketsRecv.Store(packetsRecv)
	d.enginePacketsDrop.Store(packetsDrop)
	d.engineQueueDepth.Store(queueDepth)
	d.engineDBBufUsage.Store(dbBufUsage)
}

// ---- 布局组装 ----

// buildLayout 创建所有控件并委托 rebuildLayout() 组装比例网格。
// 仅在 Start() 初始化时调用一次；窗口 resize 时复用已创建的控件。
func (d *Dashboard) buildLayout() {
	termW, _ := ui.TerminalDimensions()

	// 从 components 包获取控件（仅初始化一次）
	d.bannerPara = components.NewBanner()
	d.infoPara = components.NewInfoCard()
	d.corePara = components.NewCoreCard()
	d.trafPara = components.NewTrafficCard()
	d.tputPara = components.NewKPICard("Throughput")
	d.queuePara = components.NewKPICard("Queue Depth")
	d.bufPara = components.NewKPICard("Buffer Usage")
	d.peakPara = components.NewKPICard("Traffic")
	d.graphGroup = components.NewSparklineGroup()
	d.alertTable = components.NewAlertTable(termW)
	d.footerPara = components.NewFooter()

	// 组装网格布局
	d.rebuildLayout()
}

// rebuildLayout 基于当前终端尺寸重建 Grid 控件树。
// 控件指针复用，仅重新计算比例坐标；供 buildLayout() 与 handleResize() 共用。
func (d *Dashboard) rebuildLayout() {
	termW, termH := ui.TerminalDimensions()

	// hub 子网格（三卡片水平排列）
	hubGrid := ui.NewGrid()
	hubGrid.Set(
		ui.NewRow(1.0/3.0, d.infoPara),
		ui.NewRow(1.0/3.0, d.corePara),
		ui.NewRow(1.0/3.0, d.trafPara),
	)

	// KPI 卡片子网格（四卡片水平排列）
	cardsGrid := ui.NewGrid()
	cardsGrid.Set(
		ui.NewRow(0.25, d.tputPara),
		ui.NewRow(0.25, d.queuePara),
		ui.NewRow(0.25, d.bufPara),
		ui.NewRow(0.25, d.peakPara),
	)

	// 根网格（垂直堆叠，比例自适应）
	d.grid = ui.NewGrid()
	d.grid.Set(
		ui.NewCol(0.22, d.bannerPara), // 横幅（含 logo + 文本）
		ui.NewCol(0.12, hubGrid),      // 三卡片中枢
		ui.NewCol(0.14, cardsGrid),    // KPI 四卡片
		ui.NewCol(0.10, d.graphGroup), // 流量图
		ui.NewCol(0.37, d.alertTable), // 告警表格
		ui.NewCol(0.05, d.footerPara), // 页脚
	)
	// 根 Grid 的 SetRect 是整棵控件树坐标传播的入口。
	// 不调用此方法，Grid.Draw() 基于零矩形 (0,0,0,0) 为所有子控件分配 ~1px 坐标。
	d.grid.SetRect(0, 0, termW, termH)
}
