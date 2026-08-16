// Package ringbuffer 保存最近的告警历史，并把新告警扇出给活跃的 SSE 订阅者。
package ringbuffer

import "sync"

// Alert 是 Rust 数据面上报、由 Web UI 渲染的规范化事件。
type Alert struct {
	Timestamp   int64  `json:"timestamp"`
	SrcIP       string `json:"src_ip"`
	DstIP       string `json:"dst_ip"`
	SrcPort     uint16 `json:"src_port"`
	DstPort     uint16 `json:"dst_port"`
	Rule        string `json:"rule"`
	Severity    uint8  `json:"severity"`
	Fingerprint string `json:"fingerprint"`
}

// Ring 是固定容量的环形缓冲，附带逐订阅者扇出。
type Ring struct {
	mu      sync.Mutex
	buf     []Alert
	head    int
	size    int
	subs    map[uint64]chan Alert
	nextSub uint64
	dropped uint64
}

func New(capacity int) *Ring {
	if capacity < 1 {
		capacity = 1
	}
	return &Ring{
		buf:  make([]Alert, capacity),
		subs: make(map[uint64]chan Alert),
	}
}

// Add 存储告警并以非阻塞方式广播给每个订阅者。
func (r *Ring) Add(a Alert) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.size == len(r.buf) {
		r.buf[r.head] = a
		r.head = (r.head + 1) % len(r.buf)
		r.dropped++
	} else {
		r.buf[(r.head+r.size)%len(r.buf)] = a
		r.size++
	}
	for _, ch := range r.subs {
		select {
		case ch <- a:
		default: // 慢订阅者丢弃本条告警，而不是阻塞 ingest
		}
	}
}

// RingStats 是环形缓冲的运行指标。
type RingStats struct {
	Capacity int    `json:"capacity"`
	Size     int    `json:"size"`
	Dropped  uint64 `json:"dropped"`
}

// Stats 返回容量、当前条数与满载时被覆盖的告警数。
func (r *Ring) Stats() RingStats {
	r.mu.Lock()
	defer r.mu.Unlock()
	return RingStats{Capacity: len(r.buf), Size: r.size, Dropped: r.dropped}
}

// Snapshot 按时间顺序返回已存储的告警。
func (r *Ring) Snapshot() []Alert {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make([]Alert, 0, r.size)
	for i := 0; i < r.size; i++ {
		out = append(out, r.buf[(r.head+i)%len(r.buf)])
	}
	return out
}

// Subscribe 注册一条带缓冲的实时告警通道。
func (r *Ring) Subscribe() (uint64, <-chan Alert) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.nextSub++
	ch := make(chan Alert, 256)
	r.subs[r.nextSub] = ch
	return r.nextSub, ch
}

// Unsubscribe 移除并关闭订阅者通道。
func (r *Ring) Unsubscribe(id uint64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if ch, ok := r.subs[id]; ok {
		delete(r.subs, id)
		close(ch)
	}
}
