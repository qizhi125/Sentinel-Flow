package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/yourname/sentinel-flow/pkg/engine"
	"gopkg.in/yaml.v3"
)

var (
	iface   = flag.String("i", "lo", "网络接口名称 (e.g., eth0, lo)")
	rules   = flag.String("r", "./configs/rules.yaml", "YAML规则配置文件路径")
	workers = flag.Int("w", 4, "工作线程数量 (1-64)")
	ebpf    = flag.Bool("ebpf", false, "启用 eBPF/AF_XDP 零拷贝捕获")
	help    = flag.Bool("h", false, "显示帮助信息")
)

// RuleConfig 用于映射 YAML 结构
type RuleConfig struct {
	Rules []engine.Rule `yaml:"rules"`
}

// loadRulesFromFile 读取并解析 YAML 规则文件
func loadRulesFromFile(filepath string) ([]engine.Rule, error) {
	data, err := os.ReadFile(filepath)
	if err != nil {
		return nil, err
	}
	var config RuleConfig
	if err := yaml.Unmarshal(data, &config); err != nil {
		return nil, err
	}
	return config.Rules, nil
}

func main() {
	flag.Parse()
	if *help {
		fmt.Fprintf(os.Stderr, "Sentinel-Flow IDS v2.0\n")
		flag.PrintDefaults()
		os.Exit(0)
	}

	fmt.Printf("🛡️  Sentinel-Flow IDS v2.0 (YAML 动态规则版)\n")
	fmt.Printf("⚙️  配置: 接口=%s, 规则=%s, 线程=%d, eBPF=%v\n\n", *iface, *rules, *workers, *ebpf)

	// 注意：这里调用你 binding.go 中实际的方法，如果是 NewEngineWithConfig 请替换
	e := engine.NewEngine(*iface, *rules)
	defer e.Close()

	fmt.Printf("📦 正在从 %s 加载检测规则...\n", *rules)
	rulesList, err := loadRulesFromFile(*rules)
	if err != nil {
		fmt.Fprintf(os.Stderr, "⚠️ 读取规则失败: %v (将使用空规则集启动)\n", err)
	} else {
		fmt.Printf("✅ 成功解析 %d 条规则，正在下发至 C++ 引擎...\n", len(rulesList))
		for _, r := range rulesList {
			e.AddRule(r)
		}
	}

	// 触发底层 C++ 引擎重新编译 Aho-Corasick 自动机
	e.ReloadRules()
	fmt.Println("✅ 底层自动机编译就绪")

	if err := e.Start(); err != nil {
		fmt.Printf("❌ 引擎启动错误: %v\n", err)
		os.Exit(1)
	}

	fmt.Println("🛰️  引擎正在运行。按 Ctrl+C 停止...\n")

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	fmt.Println("\n\n🛑 正在停止引擎并清理资源...")
	e.Stop()
}