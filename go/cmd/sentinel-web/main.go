// Command sentinel-web 是 Go 控制面：HTTP ingest、SSE 事件流与嵌入式前端。
package main

import (
	"flag"
	"log"
	"net/http"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/httpapi"
	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
)

func main() {
	addr := flag.String("addr", ":21318", "HTTP 监听地址")
	flag.Parse()

	rb := ringbuffer.New(1000)
	server := &http.Server{
		Addr:              *addr,
		Handler:           httpapi.NewHandler(rb),
		ReadHeaderTimeout: 5 * time.Second,
	}
	log.Printf("sentinel-web 正在监听 %s", *addr)
	if err := server.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}
