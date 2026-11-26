package sources

import (
	"context"
	"testing"
	"time"
)

func TestClockPostsEachNewMinute(t *testing.T) {
	zone, _ := time.LoadLocation("Europe/Stockholm")
	now := time.Date(2026, 11, 26, 13, 4, 10, 0, time.UTC)
	c := NewClock(zone, 12*time.Second)
	c.Now = func() time.Time { return now }

	e, _ := c.Poll(context.Background())
	if e == nil || e.Content.Text != "14:04" || e.Content.Title != "Thursday 26 November" || e.Priority != "ambient" || e.Duration != 12 {
		t.Fatalf("first: %+v", e)
	}
	now = now.Add(30 * time.Second)
	if e, _ := c.Poll(context.Background()); e != nil {
		t.Fatalf("same minute sent again: %+v", e)
	}
	now = now.Add(30 * time.Second)
	if e, _ := c.Poll(context.Background()); e == nil || e.Content.Text != "14:05" {
		t.Fatalf("next minute: %+v", e)
	}
}
