// Package httpapi 组装 ingest、SSE 流与嵌入式前端。
package httpapi

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"sync"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
	"github.com/qizhi125/Sentinel-Flow/go/web"
)

const maxBodyBytes = 1 << 20

// Options 控制 ingest 速率限制。
type Options struct {
	IngestRate  float64 // 每秒允许的 ingest 请求数
	IngestBurst float64 // 突发容量
}

// DefaultOptions 返回控制面的默认限速配置。
func DefaultOptions() Options {
	return Options{IngestRate: 100, IngestBurst: 200}
}

func NewHandler(rb *ringbuffer.Ring) http.Handler {
	return NewHandlerWithOptions(rb, DefaultOptions())
}

func NewHandlerWithOptions(rb *ringbuffer.Ring, opts Options) http.Handler {
	limiter := newTokenBucket(opts.IngestRate, opts.IngestBurst)
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/ingest", ingest(rb, limiter))
	mux.HandleFunc("GET /v1/events", events(rb))
	mux.HandleFunc("GET /v1/health", health)
	mux.HandleFunc("GET /v1/stats", stats(rb, limiter))
	mux.Handle("GET /", http.FileServer(http.FS(web.Assets)))
	return mux
}

// tokenBucket 是固定速率、带突发的令牌桶，保护 ingest 入口。
type tokenBucket struct {
	mu     sync.Mutex
	rate   float64
	burst  float64
	tokens float64
	last   time.Time
	denied uint64
}

func newTokenBucket(rate, burst float64) *tokenBucket {
	return &tokenBucket{rate: rate, burst: burst, tokens: burst, last: time.Now()}
}

func (b *tokenBucket) allow() bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	now := time.Now()
	b.tokens = math.Min(b.burst, b.tokens+now.Sub(b.last).Seconds()*b.rate)
	b.last = now
	if b.tokens < 1 {
		b.denied++
		return false
	}
	b.tokens--
	return true
}

func (b *tokenBucket) deniedCount() uint64 {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.denied
}

// health 返回 200，供部署与 CI 探活。
func health(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	io.WriteString(w, "ok\n")
}

// stats 返回环形缓冲与限速的运行指标。
func stats(rb *ringbuffer.Ring, limiter *tokenBucket) http.HandlerFunc {
	return func(w http.ResponseWriter, _ *http.Request) {
		payload := struct {
			ringbuffer.RingStats
			RateLimited uint64 `json:"rate_limited"`
		}{RingStats: rb.Stats(), RateLimited: limiter.deniedCount()}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(payload)
	}
}

func ingest(rb *ringbuffer.Ring, limiter *tokenBucket) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !limiter.allow() {
			log.Printf("审计：ingest 限速拒绝 来源=%s", r.RemoteAddr)
			http.Error(w, "请求过于频繁", http.StatusTooManyRequests)
			return
		}
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
