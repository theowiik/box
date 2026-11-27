package sources

import (
	"context"
	"net/http"
	"net/http/httptest"
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

func TestGlanceShowsTimeWeatherAndSmiley(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`{"current":{"temperature_2m":3.4,"weather_code":0,"is_day":1}}`))
	}))
	defer srv.Close()
	old := ForecastURL
	ForecastURL = srv.URL
	defer func() { ForecastURL = old }()

	zone, _ := time.LoadLocation("Europe/Stockholm")
	now := time.Date(2026, 11, 26, 13, 4, 10, 0, time.UTC)
	g := NewGlance(NewWeather(Place{Name: "Stockholm"}, false, 10*time.Minute), zone, 12*time.Second)
	g.Now = func() time.Time { return now }
	e, err := g.Poll(context.Background())
	if err != nil || e == nil || e.Content.Type != "glance" || e.Content.Time != "14:04" || e.Content.Temp != "3°" || e.Content.Sky != "sun" || e.Content.Mood != "happy" || e.Content.Date != "Thursday 26 November" || e.Content.Color != "#FFD54F" {
		t.Fatalf("got %+v, %v", e, err)
	}
	if e, _ := g.Poll(context.Background()); e != nil {
		t.Fatalf("unchanged sent again: %+v", e)
	}
}
