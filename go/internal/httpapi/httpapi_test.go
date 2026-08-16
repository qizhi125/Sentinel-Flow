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

	payload := `{"timestamp":1,"src_ip":"192.168.1.10","dst_ip":"8.8.8.8","src_port":50000,"dst_port":443,"rule":"SyntheticDemo-Beacon","severity":6,"fingerprint":"ea1e247991e541e39bf918cb7cfa5139"}`
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
		if alert.Rule != "SyntheticDemo-Beacon" {
			t.Fatalf("streamed rule = %q", alert.Rule)
		}
		return
	}
}
