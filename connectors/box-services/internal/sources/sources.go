// Package sources produces events on timers: the weather and feeds. The
// runner polls each on its own goroutine and posts what it gets to the box.
package sources

import (
	"context"
	"log/slog"
	"time"

	"github.com/theowiik/box/connectors/box-services/internal/client"
)

// A Source is polled for something to show.
type Source interface {
	Name() string
	Interval() time.Duration
	// Poll returns an event, or nil for nothing new. It may block.
	Poll(ctx context.Context) (*client.Event, error)
}

// MinInterval stops a typo from becoming a hot loop.
var MinInterval = time.Second

// Sender is what the runner posts to: a *client.Box.
type Sender interface {
	Send(client.Event) (client.Accepted, error)
}

// PollOnce polls one source and delivers its event. True if one was sent.
func PollOnce(ctx context.Context, box Sender, s Source) bool {
	event, err := s.Poll(ctx)
	if err != nil {
		slog.Warn("poll failed", "source", s.Name(), "err", err)
		return false
	}
	if event == nil {
		return false
	}
	accepted, err := box.Send(*event)
	if err != nil {
		slog.Warn("not delivered", "source", s.Name(), "err", err)
		return false
	}
	slog.Info("sent", "source", s.Name(), "priority", accepted.Priority, "id", accepted.ID)
	return true
}

// Run polls every source until ctx ends: each at once, then on its timer.
// A source that fails, or a box that is off, is logged and retried next
// time; nothing here needs a restart.
func Run(ctx context.Context, box Sender, sources []Source) {
	done := make(chan struct{})
	for _, s := range sources {
		go func() {
			defer func() { done <- struct{}{} }()
			ticker := time.NewTicker(max(s.Interval(), MinInterval))
			defer ticker.Stop()
			for {
				PollOnce(ctx, box, s)
				select {
				case <-ctx.Done():
					return
				case <-ticker.C:
				}
			}
		}()
	}
	for range sources {
		<-done
	}
}
