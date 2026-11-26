package sources

import (
	"context"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
)

// The idle rotation is the box's own: each source owns one ambient card and
// the box takes turns between them when nothing else is on. These sources
// only keep their card there, so they cost a post a minute (the clock) or
// one upload every half hour (each visual, in case the box restarted).

// Clock keeps the time, and the day under it, as an ambient card.
type Clock struct {
	Zone  *time.Location
	Dwell time.Duration
	Now   func() time.Time // for tests
	last  string
	sent  time.Time
}

func NewClock(zone *time.Location, dwell time.Duration) *Clock {
	return &Clock{Zone: zone, Dwell: dwell, Now: time.Now}
}

func (c *Clock) Name() string { return "clock" }

// Interval is short so a new minute shows within seconds; most polls send nothing.
func (c *Clock) Interval() time.Duration { return 5 * time.Second }

func (c *Clock) Poll(ctx context.Context) (*client.Event, error) {
	now := c.Now().In(c.Zone)
	text := now.Format("15:04")
	// Resend now and then even when unchanged, in case the box restarted.
	if text == c.last && now.Sub(c.sent) < 10*time.Minute {
		return nil, nil
	}
	c.last, c.sent = text, now
	e := client.Text(c.Name(), now.Format("Monday 2 January"), text)
	e.Priority = "ambient"
	e.Duration = c.Dwell.Seconds()
	return &e, nil
}

// IdleVisual keeps one built-in visual in the rotation.
type IdleVisual struct {
	Visual string
	Dwell  time.Duration
}

// IdleVisualPrefix names idle visuals' sources: "idle-rainbow".
const IdleVisualPrefix = "idle-"

func (v *IdleVisual) Name() string            { return IdleVisualPrefix + v.Visual }
func (v *IdleVisual) Interval() time.Duration { return 30 * time.Minute }

func (v *IdleVisual) Poll(ctx context.Context) (*client.Event, error) {
	e := client.Visual(v.Name(), v.Visual)
	e.Priority = "ambient"
	e.Duration = v.Dwell.Seconds()
	return &e, nil
}
