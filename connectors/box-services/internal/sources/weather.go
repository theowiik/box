package sources

import (
	"context"
	"fmt"
	"hash/fnv"
	"math/rand/v2"
	"time"

	"github.com/theowiik/box/connectors/box-services/internal/client"
)

// BucketSeconds: the mock is stable within a bucket, so repeated polls agree.
const BucketSeconds = 300

type condition struct{ Label, Color string }

var conditions = []condition{
	{"Sunny", "#FFD54F"}, {"Clear", "#64B5F6"}, {"Cloudy", "#9E9E9E"}, {"Rainy", "#4FC3F7"},
	{"Snow", "#B0BEC5"}, {"Windy", "#80CBC4"}, {"Storm", "#FF7043"}, {"Foggy", "#78909C"},
}

// Reading is one (mock) weather report.
type Reading struct {
	City      string
	Temp      int
	Condition string
	Color     string
}

// Weather is a mock weather source: an ambient card for the idle rotation,
// refreshed in place every minute.
type Weather struct {
	City  string
	Dwell time.Duration // seconds on screen per turn in the rotation
	Every time.Duration
	Now   func() time.Time
}

func NewWeather(city string) *Weather {
	return &Weather{City: city, Dwell: 12 * time.Second, Every: time.Minute, Now: time.Now}
}

func (w *Weather) Name() string            { return "weather" }
func (w *Weather) Interval() time.Duration { return w.Every }

// Get is the reading at t: deterministic per city and five-minute bucket.
func (w *Weather) Get(t time.Time) Reading {
	h := fnv.New64a()
	fmt.Fprintf(h, "%s:%d", w.City, t.Unix()/BucketSeconds)
	seed := h.Sum64()
	rng := rand.New(rand.NewPCG(seed, seed>>1))
	c := conditions[rng.IntN(len(conditions))]
	return Reading{City: w.City, Temp: rng.IntN(41) - 5, Condition: c.Label, Color: c.Color}
}

func (w *Weather) Poll(context.Context) (*client.Event, error) {
	r := w.Get(w.Now())
	e := client.Text(w.Name(), r.City, fmt.Sprintf("%d° %s", r.Temp, r.Condition))
	e.Priority = "ambient"
	e.Duration = w.Dwell.Seconds()
	e.Content.Color = r.Color
	return &e, nil
}
