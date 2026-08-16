package ringbuffer

import "testing"

func TestOverwritesOldestWhenFull(t *testing.T) {
	ring := New(3)
	for i := int64(1); i <= 5; i++ {
		ring.Add(Alert{Timestamp: i, Rule: "rule"})
	}
	snapshot := ring.Snapshot()
	if len(snapshot) != 3 {
		t.Fatalf("snapshot length = %d, want 3", len(snapshot))
	}
	if snapshot[0].Timestamp != 3 || snapshot[2].Timestamp != 5 {
		t.Fatalf("snapshot order = %v", snapshot)
	}
}

func TestSubscribeReceivesLiveAlerts(t *testing.T) {
	ring := New(10)
	id, ch := ring.Subscribe()
	defer ring.Unsubscribe(id)

	ring.Add(Alert{Rule: "live"})
	alert := <-ch
	if alert.Rule != "live" {
		t.Fatalf("received rule = %q, want live", alert.Rule)
	}
}
