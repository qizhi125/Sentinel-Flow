package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"runtime"
	"sync"
	"syscall"
	"time"

	"github.com/qizhi125/Sentinel-Flow/pkg/config"
	"github.com/qizhi125/Sentinel-Flow/pkg/engine"
	"github.com/qizhi125/Sentinel-Flow/pkg/ui"
)

var (
	configFile = flag.String("c", "configs/rules.yaml", "规则配置文件路径（YAML 格式）")
	iface      = flag.String("i", "lo", "网络接口名称")
)

func main() {
	// 主线程必须在任何 CGO 调用前锁定，确保 termui 终端初始化时 OS 线程状态干净。
	// 此锁伴随整个进程生命周期（main 返回即进程退出，无需解锁）。
	runtime.LockOSThread()

	flag.Parse()

	// 阶段 1：创建仪表盘与引擎（UI 尚未接管终端，禁止 CGO 调用）

	dash := ui.NewDashboard(8, *iface)

	eng, err := engine.New()
	if err != nil {
		fmt.Fprintf(os.Stderr, "引擎创建失败: %v\n", err)
		os.Exit(1)
	}
	defer eng.Close()

	rules, err := config.LoadRules(*configFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "加载规则失败: %v\n", err)
		os.Exit(1)
	}
	if err := applyRules(eng, rules); err != nil {
		fmt.Fprintf(os.Stderr, "应用规则失败: %v\n", err)
		os.Exit(1)
	}
	dash.UpdateRuleCount(len(rules))

	if err := eng.BuildMatcher(); err != nil {
		fmt.Fprintf(os.Stderr, "构建自动机失败: %v\n", err)
		os.Exit(1)
	}

	// 告警回调：构建 AlertRecord 并推送到 UI（线程安全）
	eng.SetAlertCallback(func(event engine.AlertEvent) {
		srcEndpoint := fmt.Sprintf("%s:%d", event.SrcIP, event.SrcPort)
		dstEndpoint := fmt.Sprintf("%s:%d", event.DstIP, event.DstPort)
		dash.SendAlert(ui.AlertRecord{
			Timestamp: time.Now().Format("15:04:05"),
			RuleID:    event.RuleID,
			Protocol:  event.Protocol,
			Message: fmt.Sprintf("%s: %s → %s 命中规则 %d",
				event.Protocol, srcEndpoint, dstEndpoint, event.RuleID),
			Level: mapLevel(event.RuleID),
			Src:   srcEndpoint,
			Dst:   dstEndpoint,
		})
	})

	// 遥测统计回调：推送真实引擎指标至仪表面板
	// CGO 回调在 C++ 统计线程上下文执行，仅执行原子存储（非 UI 操作）
	eng.SetStatsCallback(func(stats engine.EngineStats) {
		if stats.HasFatalError {
			log.Printf("[engine] FATAL: Pipeline 消费者线程已异常退出，触发 UI 安全关机")
			dash.Quit()
			return
		}
		dash.UpdateTelemetry(stats.PacketsReceived, stats.PacketsDropped,
			uint64(stats.QueueDepth), uint64(stats.DBBufferUsage))
	})

	// defer 确保引擎在任何退出路径上被停止
	engStarted := true
	defer func() {
		if engStarted {
			eng.Stop()
		}
	}()

	// 初始化 TUI 遥测日志（termui 接管终端后，log 输出仅写入文件）
	f, err := os.OpenFile("sentinel-tui.log", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		fmt.Fprintf(os.Stderr, "TUI 日志文件创建失败: %v\n", err)
	} else {
		log.SetOutput(f)
		defer f.Close()
	}

	// 文件热重载监控（后台 goroutine）
	var reloadMu sync.Mutex
	watcher, err := config.NewWatcher(*configFile, func(newRules []config.Rule) error {
		reloadMu.Lock()
		defer reloadMu.Unlock()
		if err := applyRules(eng, newRules); err != nil {
			return err
		}
		dash.UpdateRuleCount(len(newRules))
		return nil
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "[watcher] 启动文件监控失败: %v\n", err)
	} else {
		go watcher.Start()
		defer watcher.Stop()
	}

	// 信号处理：SIGINT/SIGTERM 通知 UI 安全退出
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		log.Printf("[signal] 收到信号 %v，通知 UI 退出", sig)
		dash.Quit()
	}()

	// 引擎启动：CGO 调用必须在独立 OS 线程执行（主线程已由 LockOSThread 锁定供 termui 独占）
	go func() {
		if err := eng.Start(*iface); err != nil {
			log.Printf("[engine] 启动失败: %v", err)
			dash.Quit()
		}
	}()

	// 统计轮询启动：StatsLoop 每秒向主循环推送触发信号
	go dash.StatsLoop()

	// 阶段 2：UI 初始化（在主线程执行，终端由 termui 接管）
	// 用 recover 包裹，防止 termui 内部 panic 跳过 defer 清理链

	func() {
		defer func() {
			if r := recover(); r != nil {
				log.Printf("[main] PANIC 恢复 dash.Start panic=%v", r)
				fmt.Fprintf(os.Stderr, "仪表盘异常崩溃: %v\n", r)
			}
		}()
		if err := dash.Start(); err != nil {
			log.Printf("[main] 仪表盘退出 err=%v", err)
			fmt.Fprintf(os.Stderr, "仪表盘异常退出: %v\n", err)
		}
	}()

	// 等待 done 通道关闭（双重保险：Start 退出后 done 应已关闭）
	select {
	case <-dash.Done():
	default:
	}

	// 阶段 3：UI 退出后的清理（defer 栈逆序执行：watcher.Stop → f.Close → eng.Stop → eng.Close）

	log.Printf("[main] 仪表盘安全退出，累积告警=%d", dash.TotalAlerts())

	fmt.Fprintln(os.Stderr)
	fmt.Fprintln(os.Stderr, "── 关机总结 ─────────────────────────────────────")
	fmt.Fprintf(os.Stderr, "  累积告警:  %d\n", dash.TotalAlerts())
	fmt.Fprintf(os.Stderr, "  退出码:    0 (正常)\n")
	fmt.Fprintln(os.Stderr, "──────────────────────────────────────────────────")
}

// applyRules 清空现有规则、添加新规则、并重新构建自动机。
func applyRules(eng *engine.Engine, rules []config.Rule) error {
	eng.ClearRules()

	for _, r := range rules {
		if err := eng.AddRule(r.Pattern, r.ID); err != nil {
			return fmt.Errorf("添加规则 %d 失败: %w", r.ID, err)
		}
	}

	if err := eng.BuildMatcher(); err != nil {
		return fmt.Errorf("构建自动机失败: %w", err)
	}

	return nil
}

// mapLevel 根据规则 ID 范围映射告警等级。
func mapLevel(ruleID int) string {
	switch {
	case ruleID >= 1000 && ruleID < 2000:
		return "HIGH"
	case ruleID >= 2000 && ruleID < 3000:
		return "MED"
	case ruleID >= 3000 && ruleID < 4000:
		return "LOW"
	default:
		return "INFO"
	}
}
