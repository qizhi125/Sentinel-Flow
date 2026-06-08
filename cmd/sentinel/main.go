package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/qizhi125/Sentinel-Flow/pkg/config"
	"github.com/qizhi125/Sentinel-Flow/pkg/engine"
)

var (
	configFile = flag.String("c", "configs/rules.yaml", "规则配置文件路径（YAML 格式）")
	iface      = flag.String("i", "lo", "网络接口名称")
)

func main() {
	flag.Parse()

	fmt.Println("Sentinel-Flow NIDS — Go 控制面")
	fmt.Printf("接口: %s, 规则文件: %s\n", *iface, *configFile)

	// 创建引擎
	eng, err := engine.New()
	if err != nil {
		fmt.Fprintf(os.Stderr, "引擎创建失败: %v\n", err)
		os.Exit(1)
	}
	defer eng.Close()

	// 加载规则
	rules, err := config.LoadRules(*configFile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "加载规则失败: %v\n", err)
		os.Exit(1)
	}
	if err := applyRules(eng, rules); err != nil {
		fmt.Fprintf(os.Stderr, "应用规则失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Printf("已加载 %d 条活跃规则\n", len(rules))

	// 构建 AC 自动机
	fmt.Println("构建检测引擎...")
	if err := eng.BuildMatcher(); err != nil {
		fmt.Fprintf(os.Stderr, "构建自动机失败: %v\n", err)
		os.Exit(1)
	}

	// 启动引擎
	fmt.Printf("启动引擎 (%s)...\n", *iface)
	if err := eng.Start(*iface); err != nil {
		fmt.Fprintf(os.Stderr, "引擎启动失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("引擎运行中, Ctrl+C 停止")

	// 热重载互斥锁 — 防止并发 BuildMatcher
	var reloadMu sync.Mutex

	// 启动规则文件热重载监控
	watcher, err := config.NewWatcher(*configFile, func(newRules []config.Rule) error {
		reloadMu.Lock()
		defer reloadMu.Unlock()
		return applyRules(eng, newRules)
	})
	if err != nil {
		fmt.Fprintf(os.Stderr, "[watcher] 启动文件监控失败: %v\n", err)
	} else {
		go watcher.Start()
		defer watcher.Stop()
	}

	// 等待信号
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

loop:
	for {
		select {
		case <-sigCh:
			fmt.Println("\n收到停止信号")
			break loop
		case <-ticker.C:
			fmt.Println("[INFO] 引擎运行正常")
		}
	}

	fmt.Println("停止引擎...")
	eng.Stop()
	fmt.Println("引擎已停止")
}

// applyRules 清空现有规则、添加新规则、并重新构建自动机。
// 调用方负责持有 reloadMu 互斥锁。
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
