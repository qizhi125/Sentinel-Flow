package httpapi

import (
	"bufio"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
)

func TestIngestThenStream(t *testing.T) {
	ring := ringbuffer.New(10)
	server := httptest.NewServer(NewHandler(ring))
	defer server.Close()

	payload := `{"timestamp":1,"src_ip":"192.0.2.1","dst_ip":"198.51.100.1","src_port":40000,"dst_port":443,"rule":"test-rule","severity":6,"fingerprint":"00000000000000000000000000000000"}`
	resp, err := http.Post(server.URL+"/v1/ingest", "application/json", strings.NewReader(payload))
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusNoContent {
		t.Fatalf("ingest status = %d, want %d", resp.StatusCode, http.StatusNoContent)
	}

	stream, err := http.Get(server.URL + "/v1/events")
	if err != nil {
		t.Fatal(err)
	}
	defer stream.Body.Close()

	reader := bufio.NewReader(stream.Body)
	deadline := time.After(3 * time.Second)
	for {
		select {
		case <-deadline:
			t.Fatal("等待 SSE 事件超时")
		default:
		}
		line, err := reader.ReadString('\n')
		if err != nil {
			t.Fatal(err)
		}
		if !strings.HasPrefix(line, "data: ") {
			continue
		}
		var alert ringbuffer.Alert
		if err := json.Unmarshal([]byte(strings.TrimSpace(strings.TrimPrefix(line, "data: "))), &alert); err != nil {
			t.Fatal(err)
		}
		if alert.Rule != "test-rule" {
			t.Fatalf("streamed rule = %q", alert.Rule)
		}
		return
	}
}

func TestHealth(t *testing.T) {
	ring := ringbuffer.New(10)
	server := httptest.NewServer(NewHandler(ring))
	defer server.Close()

	resp, err := http.Get(server.URL + "/v1/health")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("health status = %d, want %d", resp.StatusCode, http.StatusOK)
	}
}
