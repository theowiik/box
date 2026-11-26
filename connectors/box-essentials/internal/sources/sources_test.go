package sources

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
)

const rss = `<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><title>En Stor Stark Review</title>
<item><title>Bistro Löfqvist å Vi</title><link>https://example.se/b</link>
<guid isPermaLink="false">review:b</guid></item>
<item><title>Beerista</title><link>https://example.se/a</link>
<guid isPermaLink="false">review:a</guid></item>
<item><title>No guid or link</title></item>
</channel></rss>`

const atom = `<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
<entry><id>tag:x,1</id><title>Release 1.2</title><link href="https://example.com/1.2"/></entry>
</feed>`

func TestParse(t *testing.T) {
	items, err := Parse([]byte(rss))
	if err != nil || len(items) != 2 || items[0].Title != "Bistro Löfqvist å Vi" || items[1].GUID != "review:a" {
		t.Fatalf("rss: %+v %v", items, err)
	}
	items, err = Parse([]byte(atom))
	if err != nil || len(items) != 1 || items[0].Link != "https://example.com/1.2" {
		t.Fatalf("atom: %+v %v", items, err)
	}
}

// feedServer honours ETags and counts full responses.
func feedServer(t *testing.T, body *string, fail *bool, full *int32) string {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if *fail {
			w.WriteHeader(503)
			return
		}
		etag := fmt.Sprintf(`"%d"`, len(*body))
		if r.Header.Get("If-None-Match") == etag {
			w.WriteHeader(304)
			return
		}
		atomic.AddInt32(full, 1)
		w.Header().Set("ETag", etag)
		fmt.Fprint(w, *body)
	}))
	t.Cleanup(srv.Close)
	return srv.URL
}

func TestFeedLifecycle(t *testing.T) {
	body, fail, full := rss, false, int32(0)
	url := feedServer(t, &body, &fail, &full)
	dir := t.TempDir()
	cfg := FeedConfig{Name: "essr", Title: "ESSR", URL: url, Show: "title"}
	f := NewFeed(cfg, dir)
	ctx := context.Background()

	if e, err := f.Poll(ctx); e != nil || err != nil {
		t.Fatalf("the first poll only takes note: %v %v", e, err)
	}
	if e, _ := f.Poll(ctx); e != nil {
		t.Fatal("nothing new yet")
	}
	if full != 1 {
		t.Fatalf("an unchanged feed is not downloaded again: %d", full)
	}
	body = `<rss><channel><item><title>New one</title><guid>review:c</guid></item>` +
		`<item><title>Newer</title><guid>review:d</guid></item>` + rss[len(`<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><title>En Stor Stark Review</title>`):]
	e, err := f.Poll(ctx)
	if err != nil || e == nil || e.Content.Title != "ESSR" || e.Content.Text != "Newer · New one" {
		t.Fatalf("a burst is one event, oldest first: %+v %v", e, err)
	}
	// Seen survives a restart, in the Python version's format.
	data, _ := os.ReadFile(filepath.Join(dir, "feed-essr.json"))
	if len(data) == 0 || data[0] != '[' {
		t.Fatalf("state: %s", data)
	}
	again := NewFeed(cfg, dir)
	if e, _ := again.Poll(ctx); e != nil {
		t.Fatal("already seen before the restart")
	}
	fail = true
	if e, err := again.Poll(ctx); e != nil || err == nil {
		t.Fatal("a failing site is an error, not an event")
	}
}

func TestFeedCanPlayAVisual(t *testing.T) {
	f := NewFeed(FeedConfig{Name: "essr", Show: "beer"}, t.TempDir())
	if e := f.Event([]Item{{GUID: "a", Title: "A"}}); e.Visual != "beer" {
		t.Fatalf("%+v", e)
	}
}

func TestSeenIsBoundedAndCorruptStateIsAFirstRun(t *testing.T) {
	dir := t.TempDir()
	s := LoadSeen(filepath.Join(dir, "s.json"))
	ids := make([]string, MaxSeen+10)
	for i := range ids {
		ids[i] = fmt.Sprint(i)
	}
	s.Add(ids)
	if s.Has("0") || !s.Has(fmt.Sprint(MaxSeen+9)) {
		t.Fatal("not bounded")
	}
	os.WriteFile(filepath.Join(dir, "bad.json"), []byte("{not json"), 0o644)
	if LoadSeen(filepath.Join(dir, "bad.json")).Known {
		t.Fatal("corrupt state should count as a first run")
	}
}

func TestWeather(t *testing.T) {
	var query url.Values
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		query = r.URL.Query()
		switch r.URL.Path {
		case "/forecast":
			fmt.Fprint(w, `{"current":{"temperature_2m":-0.4,"weather_code":3,"is_day":1}}`)
		case "/search":
			fmt.Fprint(w, `{"results":[{"name":"Göteborg","admin1":"Västra Götaland","country":"Sweden","latitude":57.7,"longitude":11.97}]}`)
		default:
			http.NotFound(w, r)
		}
	}))
	defer srv.Close()
	ForecastURL, GeocodingURL = srv.URL+"/forecast", srv.URL+"/search"

	w := NewWeather(Place{Name: "Stockholm", Latitude: 59.3293, Longitude: 18.0686}, false, 0)
	e, err := w.Poll(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if e.Priority != "ambient" || e.Content.Title != "Stockholm" || e.Content.Text != "0° Cloudy" ||
		e.Content.Color == "" || e.Duration != 12 || w.Interval() != 10*time.Minute {
		t.Fatalf("%+v", e)
	}
	if query.Get("latitude") != "59.3293" || query.Get("temperature_unit") != "" {
		t.Fatalf("asked for %v", query)
	}
	w.Fahrenheit = true
	w.Poll(context.Background())
	if query.Get("temperature_unit") != "fahrenheit" {
		t.Fatalf("asked for %v", query)
	}

	places, err := FindPlaces(context.Background(), http.DefaultClient, "gothenburg")
	if err != nil || len(places) != 1 || places[0].Name != "Göteborg" || places[0].Latitude != 57.7 {
		t.Fatalf("%v %+v", err, places)
	}
	if query.Get("name") != "gothenburg" {
		t.Fatalf("asked for %v", query)
	}

	ForecastURL = srv.URL + "/missing"
	if _, err := w.Poll(context.Background()); err == nil {
		t.Fatal("a failed forecast is an error, not a card")
	}
}

func TestWeatherConditions(t *testing.T) {
	for code, want := range map[int]string{0: "Sunny", 2: "Partly cloudy", 45: "Foggy", 53: "Drizzle",
		63: "Rainy", 81: "Rainy", 73: "Snow", 86: "Snow", 95: "Storm", 42: "Unknown"} {
		if got, _ := Condition(code, true); got != want {
			t.Errorf("%d: %s, want %s", code, got, want)
		}
	}
	if got, _ := Condition(0, false); got != "Clear" {
		t.Errorf("night: %s", got)
	}
}

type recorder struct {
	mu   sync.Mutex
	sent []client.Event
	fail bool
}

func (r *recorder) Send(e client.Event) (client.Accepted, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.fail {
		return client.Accepted{}, errors.New("box is off")
	}
	r.sent = append(r.sent, e)
	return client.Accepted{ID: "x"}, nil
}

type counter struct{ n atomic.Int32 }

func (c *counter) Name() string            { return "counter" }
func (c *counter) Interval() time.Duration { return 10 * time.Millisecond }
func (c *counter) Poll(context.Context) (*client.Event, error) {
	e := client.Text("counter", "", fmt.Sprint(c.n.Add(1)))
	return &e, nil
}

type broken struct{}

func (broken) Name() string            { return "broken" }
func (broken) Interval() time.Duration { return 10 * time.Millisecond }
func (broken) Poll(context.Context) (*client.Event, error) {
	return nil, errors.New("boom")
}

func TestRunnerSurvivesFailures(t *testing.T) {
	MinInterval = 0
	box := &recorder{}
	c := &counter{}
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()
	var reports atomic.Int32
	Run(ctx, box, []Source{c, broken{}}, func(Report) { reports.Add(1) })
	if c.n.Load() < 3 || len(box.sent) != int(c.n.Load()) {
		t.Fatalf("a broken source must not stop the others: %d polls, %d sent", c.n.Load(), len(box.sent))
	}
	if reports.Load() < 6 {
		t.Fatalf("every poll of both sources is reported: %d", reports.Load())
	}
	box.fail = true
	if r := PollOnce(context.Background(), box, c); r.Sent || r.Error == "" {
		t.Fatal("an unreachable box is not a delivery")
	}
}
