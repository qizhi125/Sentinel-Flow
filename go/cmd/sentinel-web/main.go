// Command sentinel-web 是 Go 控制面：HTTP ingest、SSE 事件流与嵌入式前端。
package main

import (
	"flag"
	"log"
	"net/http"
	"os/exec"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/httpapi"
	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
)

func main() {
	addr := flag.String("addr", "127.0.0.1:21318", "HTTP 监听地址")
	noOpen := flag.Bool("no-open", false, "启动时不自动打开浏览器")
	flag.Parse()

	rb := ringbuffer.New(1000)
	server := &http.Server{
		Addr:              *addr,
		Handler:           httpapi.NewHandler(rb),
		ReadHeaderTimeout: 5 * time.Second,
	}
	url := "http://" + *addr
	log.Printf("sentinel-web 正在监听 %s", url)
	if !*noOpen {
		openBrowser(url)
	}
	if err := server.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}

// openBrowser 尽力打开默认浏览器；失败只记录日志，不影响服务启动。
func openBrowser(url string) {
	cmd := exec.Command("xdg-open", url)
	if err := cmd.Start(); err != nil {
		log.Printf("自动打开浏览器失败: %v", err)
		return
	}
	go func() {
		_ = cmd.Wait()
	}()
}
