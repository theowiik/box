// Package sources produces events on timers: the weather and feeds. The
// runner polls each on its own goroutine and posts what it gets to the box.
package sources

import (
	"context"
	"log/slog"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
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

// Report is how one poll went.
type Report struct {
	Source string    `json:"source"`
	At     time.Time `json:"at"`
	Sent   bool      `json:"sent"`
	// Shown is what went to the box, for people: "Stockholm: 3° Cloudy".
	Shown string `json:"shown,omitempty"`
	Error string `json:"error,omitempty"`
}

// PollOnce polls one source and delivers its event.
func PollOnce(ctx context.Context, box Sender, s Source) Report {
	r := Report{Source: s.Name(), At: time.Now()}
	event, err := s.Poll(ctx)
	if err != nil {
		slog.Warn("poll failed", "source", s.Name(), "err", err)
		r.Error = err.Error()
		return r
	}
	if event == nil {
		return r
	}
	r.Shown = Describe(*event)
	accepted, err := box.Send(*event)
	if err != nil {
		slog.Warn("not delivered", "source", s.Name(), "err", err)
		r.Error = "not delivered: " + err.Error()
		return r
	}
	slog.Info("sent", "source", s.Name(), "priority", accepted.Priority, "id", accepted.ID)
	r.Sent = true
	return r
}

// Describe sums an event up in a line.
func Describe(e client.Event) string {
	switch {
	case e.Visual != "":
		return "the " + e.Visual + " visual"
	case e.Content.Type == "text" && e.Content.Title != "":
		return e.Content.Title + ": " + e.Content.Text
	case e.Content.Type == "text":
		return e.Content.Text
	}
	return e.Content.Type
}

// Run polls every source until ctx ends: each at once, then on its timer,
// handing each poll's report to report (if not nil; it may be called from
// several goroutines). A source that fails, or a box that is off, is logged
// and retried next time; nothing here needs a restart.
func Run(ctx context.Context, box Sender, sources []Source, report func(Report)) {
	done := make(chan struct{})
	for _, s := range sources {
		go func() {
			defer func() { done <- struct{}{} }()
			ticker := time.NewTicker(max(s.Interval(), MinInterval))
			defer ticker.Stop()
			for {
				r := PollOnce(ctx, box, s)
				if report != nil && ctx.Err() == nil {
					report(r)
				}
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
