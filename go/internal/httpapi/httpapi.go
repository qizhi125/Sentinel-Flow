// Package httpapi 组装 ingest、SSE 流与嵌入式前端。
package httpapi

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
	"github.com/qizhi125/Sentinel-Flow/go/web"
)

const maxBodyBytes = 1 << 20

func NewHandler(rb *ringbuffer.Ring) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/ingest", ingest(rb))
	mux.HandleFunc("GET /v1/events", events(rb))
	mux.HandleFunc("GET /v1/health", health)
	mux.Handle("GET /", http.FileServer(http.FS(web.Assets)))
	return mux
}

// health 返回 200，供部署与 CI 探活。
func health(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	io.WriteString(w, "ok\n")
}

func ingest(rb *ringbuffer.Ring) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(io.LimitReader(r.Body, maxBodyBytes))
		if err != nil {
			http.Error(w, "读取请求体失败: "+err.Error(), http.StatusBadRequest)
			return
		}
		var alert ringbuffer.Alert
		if err := json.Unmarshal(body, &alert); err != nil {
			http.Error(w, "JSON 格式错误: "+err.Error(), http.StatusBadRequest)
			return
		}
		if alert.SrcIP == "" || alert.Rule == "" {
			http.Error(w, "src_ip 与 rule 为必填字段", http.StatusBadRequest)
			return
		}
		rb.Add(alert)
		w.WriteHeader(http.StatusNoContent)
	}
}

func events(rb *ringbuffer.Ring) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "当前环境不支持流式响应", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")

		id, ch := rb.Subscribe()
		defer rb.Unsubscribe(id)

		writeEvent := func(alert ringbuffer.Alert) bool {
			payload, err := json.Marshal(alert)
			if err != nil {
				return false
			}
			if _, err := fmt.Fprintf(w, "data: %s\n\n", payload); err != nil {
				return false
			}
			flusher.Flush()
			return true
		}

		for _, alert := range rb.Snapshot() {
			if !writeEvent(alert) {
				return
			}
		}

		heartbeat := time.NewTicker(15 * time.Second)
		defer heartbeat.Stop()
		for {
			select {
			case <-r.Context().Done():
				return
			case alert := <-ch:
				if !writeEvent(alert) {
					return
				}
			case <-heartbeat.C:
				if _, err := io.WriteString(w, ": keep-alive\n\n"); err != nil {
					return
				}
				flusher.Flush()
			}
		}
	}
}
