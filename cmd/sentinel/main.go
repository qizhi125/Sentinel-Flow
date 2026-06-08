package main

import (
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/qizhi125/Sentinel-Flow/pkg/engine"
)

func main() {
	fmt.Println("Sentinel-Flow NIDS — Go Control Plane")

	// 创建引擎
	eng, err := engine.New()
	if err != nil {
		fmt.Fprintf(os.Stderr, "引擎创建失败: %v\n", err)
		os.Exit(1)
	}
	defer eng.Close()

	// 添加检测规则
	fmt.Println("加载规则...")
	rules := []struct {
		pattern string
		id      int
	}{
		{"malware_payload", 1001},
		{"sql_injection", 1002},
		{"reverse_shell", 1003},
	}
	for _, r := range rules {
		if err := eng.AddRule(r.pattern, r.id); err != nil {
			fmt.Fprintf(os.Stderr, "添加规则失败: %v\n", err)
			os.Exit(1)
		}
	}
	fmt.Printf("已添加 %d 条规则\n", eng.RuleCount())

	// 构建 AC 自动机
	fmt.Println("构建检测引擎...")
	if err := eng.BuildMatcher(); err != nil {
		fmt.Fprintf(os.Stderr, "构建自动机失败: %v\n", err)
		os.Exit(1)
	}

	// 启动引擎
	fmt.Println("启动引擎 (lo)...")
	if err := eng.Start("lo"); err != nil {
		fmt.Fprintf(os.Stderr, "引擎启动失败: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("引擎运行中, Ctrl+C 停止")

	// 等待信号
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	// 定期状态输出
	ticker := time.NewTicker(5 * time.Second)
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

	// 停止引擎
	fmt.Println("停止引擎...")
	eng.Stop()
	fmt.Println("引擎已停止")
}
