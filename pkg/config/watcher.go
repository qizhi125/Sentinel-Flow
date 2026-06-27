// 规则文件热重载监控模块。
// 使用 fsnotify 监听 YAML 规则文件变更，500ms 防抖后触发回调。
package config

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/fsnotify/fsnotify"
)

// ReloadCallback 是规则热重载的回调签名。
// 参数为重新加载后的规则列表。
type ReloadCallback func(rules []Rule) error

// Watcher 监控规则文件变更并在防抖后触发热重载。
type Watcher struct {
	filepath string        // 被监控的规则文件路径
	callback ReloadCallback // 热重载回调
	watcher  *fsnotify.Watcher

	mu      sync.Mutex
	timer   *time.Timer
	done    chan struct{}
	running bool
}

// NewWatcher 创建文件监控器。
// filepath   - 规则文件路径
// callback   - 文件变更后触发的重载回调
func NewWatcher(rulePath string, callback ReloadCallback) (*Watcher, error) {
	fw, err := fsnotify.NewWatcher()
	if err != nil {
		return nil, fmt.Errorf("创建 fsnotify 监控器失败: %w", err)
	}

    // 监听规则文件所在目录（兼容编辑器原子写入 —— 写入临时文件后重命名）
	dir := filepath.Dir(rulePath)
	if err := fw.Add(dir); err != nil {
		fw.Close()
		return nil, fmt.Errorf("监控目录失败 %s: %w", dir, err)
	}

	return &Watcher{
		filepath: rulePath,
		callback: callback,
		watcher:  fw,
		done:     make(chan struct{}),
	}, nil
}

// Start 启动监控循环（阻塞，应在 goroutine 中调用）。
func (w *Watcher) Start() {
	w.mu.Lock()
	if w.running {
		w.mu.Unlock()
		return
	}
	w.running = true
	w.mu.Unlock()

	defer w.watcher.Close()

	const debounceDelay = 500 * time.Millisecond

	for {
		select {
		case <-w.done:
			return

		case event, ok := <-w.watcher.Events:
			if !ok {
				return
			}

			// 仅处理目标文件的写入和创建事件
			if filepath.Base(event.Name) != filepath.Base(w.filepath) {
				continue
			}
			if !event.Has(fsnotify.Write) && !event.Has(fsnotify.Create) {
				continue
			}

			// 防抖：重置定时器
			w.mu.Lock()
			if w.timer != nil {
				w.timer.Stop()
			}
			w.timer = time.AfterFunc(debounceDelay, func() {
				fmt.Fprintf(os.Stderr, "[watcher] 检测到规则文件变更，正在热重载...\n")
				rules, err := LoadRules(w.filepath)
				if err != nil {
					fmt.Fprintf(os.Stderr, "[watcher] 重载规则失败: %v\n", err)
					return
				}
				if err := w.callback(rules); err != nil {
					fmt.Fprintf(os.Stderr, "[watcher] 应用规则失败: %v\n", err)
					return
				}
				fmt.Fprintf(os.Stderr, "[watcher] 规则热重载完成，共 %d 条活跃规则\n", len(rules))
			})
			w.mu.Unlock()

		case err, ok := <-w.watcher.Errors:
			if !ok {
				return
			}
			fmt.Fprintf(os.Stderr, "[watcher] 监控错误: %v\n", err)
		}
	}
}

// Stop 停止监控循环。
func (w *Watcher) Stop() {
	close(w.done)
}
