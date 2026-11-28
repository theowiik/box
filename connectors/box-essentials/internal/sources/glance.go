package sources

import (
	"context"
	"fmt"
	"log/slog"
	"math"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
)

// Glance is one ambient card with everything at a glance: the time, the
// weather and a little smiley that follows the weather. It posts when the
// weather or the day changes; the weather is asked for every Weather.Every.
type Glance struct {
	Weather *Weather
	Zone    *time.Location
	Dwell   time.Duration
	Now     func() time.Time // for tests

	reading Reading
	asked   time.Time // when the weather was last asked for
	have    bool
	last    string
	sent    time.Time
}

func NewGlance(w *Weather, zone *time.Location, dwell time.Duration) *Glance {
	return &Glance{Weather: w, Zone: zone, Dwell: dwell, Now: time.Now}
}

func (g *Glance) Name() string            { return "glance" }
func (g *Glance) Interval() time.Duration { return 5 * time.Second }

func (g *Glance) Poll(ctx context.Context) (*client.Event, error) {
	now := g.Now().In(g.Zone)
	var werr error
	if !g.have || now.Sub(g.asked) >= g.Weather.Every {
		g.asked = now // a failure waits a whole interval too, not 5 seconds
		if r, err := g.Weather.Get(ctx); err != nil {
			werr = err
		} else {
			g.reading, g.have = r, true
		}
	}
	c := client.Content{Type: "glance", Date: now.Format("Monday 2 January"), Time: now.Format("15:04"), Mood: "happy"} // the smiley is always happy
	if g.have {
		r := g.reading
		c.Temp = fmt.Sprintf("%d°", int(math.Round(r.Temp)))
		c.Condition, c.Sky, c.Color = r.Condition, r.Sky, r.Color
	}
	// The box keeps the time itself (the dots move when the minute turns), so
	// a new minute is no reason to post: a new event would restart the glance.
	key := fmt.Sprint(c.Date, c.Temp, c.Condition, c.Sky, c.Mood, c.Color)
	if key == g.last && now.Sub(g.sent) < 10*time.Minute {
		return nil, werr
	}
	g.last, g.sent = key, now
	e := client.Event{Source: g.Name(), Priority: "ambient", Duration: g.Dwell.Seconds(), Content: c}
	if werr != nil {
		slog.Warn("glance: no weather this time", "err", werr)
	}
	return &e, nil
}
