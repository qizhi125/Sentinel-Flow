package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/fsnotify/fsnotify"
	"github.com/qizhi125/Sentinel-Flow/pkg/engine"
	"gopkg.in/yaml.v3"
)

var (
	iface   = flag.String("i", "lo", "网络接口名称 (e.g., eth0, lo)")
	rules   = flag.String("r", "./configs/rules.yaml", "YAML规则配置文件路径")
	workers = flag.Int("w", 4, "工作线程数量 (1-64)")
	ebpf    = flag.Bool("ebpf", false, "启用 eBPF 捕获")
	offline = flag.String("offline", "", "离线 PCAP 路径")
	verbose = flag.Bool("v", false, "启用详细日志输出 (Verbose 模式)")
	help    = flag.Bool("h", false, "显示帮助信息")
)

func logDebug(format string, a ...interface{}) {
	if *verbose {
		fmt.Printf(format, a...)
	}
}

type RuleConfig struct {
	Rules []engine.Rule `yaml:"rules"`
}

func loadRulesFromFile(filepath string) ([]engine.Rule, error) {
	data, err := os.ReadFile(filepath)
	if err != nil { return nil, err }
	var config RuleConfig
	if err := yaml.Unmarshal(data, &config); err != nil { return nil, err }
	return config.Rules, nil
}

func watchRules(rulePath string, e *engine.Engine) {
	watcher, err := fsnotify.NewWatcher()
	if err != nil { return }
	defer func() { _ = watcher.Close() }()
	if err := watcher.Add(filepath.Dir(rulePath)); err != nil { return }

	var mu sync.Mutex
	var timer *time.Timer

	for {
		select {
		case event, ok := <-watcher.Events:
			if !ok { return }
			if !strings.HasSuffix(event.Name, filepath.Base(rulePath)) { continue }

			if event.Has(fsnotify.Write) || event.Has(fsnotify.Create) || event.Has(fsnotify.Chmod) {
				mu.Lock()
				if timer != nil { timer.Stop() }
				timer = time.AfterFunc(500*time.Millisecond, func() {
					logDebug("\n🔄 监听到 [%s] 变更，正在热重载...\n", rulePath)
					rulesList, err := loadRulesFromFile(rulePath)
					if err != nil { return }
					e.ClearRules()
					for _, r := range rulesList { e.AddRule(r) }
					e.ReloadRules()
					logDebug("✅ 规则热重载完成！当前生效: %d 条\n", len(rulesList))
				})
				mu.Unlock()
			}
		case <-watcher.Errors:
			return
		}
	}
}

func main() {
	flag.Parse()
	if *help {
		fmt.Fprintf(os.Stderr, "Sentinel-Flow IDS v2.0\n")
		flag.PrintDefaults()
		os.Exit(0)
	}

	fmt.Printf("🛡️  Sentinel-Flow IDS v2.0\n")
	e := engine.NewEngineWithConfig(*iface, *rules, *workers, *ebpf, *offline, *verbose)
	defer e.Close()

	rulesList, err := loadRulesFromFile(*rules)
	if err == nil {
		logDebug("📦 成功解析 %d 条规则，正在下发...\n", len(rulesList))
		for _, r := range rulesList { e.AddRule(r) }
	}
	e.ReloadRules()

	if err := e.Start(); err != nil {
		fmt.Printf("❌ 启动错误: %v\n", err)
		os.Exit(1)
	}

	go watchRules(*rules, e)
	logDebug("🛰️  引擎正在运行。按 Ctrl+C 停止...\n")

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	logDebug("\n\n🛑 正在停止引擎...\n")
	e.Stop()
}
