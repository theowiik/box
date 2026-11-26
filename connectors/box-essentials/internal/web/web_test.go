package web

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/service"
	"github.com/theowiik/box/connectors/box-essentials/internal/settings"
	"github.com/theowiik/box/connectors/box-essentials/internal/sources"
)

// fakeBox records events and answers status.
type fakeBox struct {
	mu     sync.Mutex
	events []map[string]any
}

func (f *fakeBox) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	defer f.mu.Unlock()
	switch r.Method + " " + r.URL.Path {
	case "POST /api/events":
		var e map[string]any
		json.NewDecoder(r.Body).Decode(&e)
		f.events = append(f.events, e)
		fmt.Fprint(w, `{"id":"1","priority":"normal","duration":10,"queued":0}`)
	case "GET /api/status":
		fmt.Fprint(w, `{"showing":{"type":"clock"},"queued":0,"ambient":[],"style":"smooth","brightness":0.8}`)
	default:
		http.NotFound(w, r)
	}
}

func (f *fakeBox) texts() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	var out []string
	for _, e := range f.events {
		c, _ := e["content"].(map[string]any)
		out = append(out, fmt.Sprint(c["title"], ": ", c["text"]))
	}
	return out
}

func TestSettingsPage(t *testing.T) {
	box := &fakeBox{}
	boxSrv := httptest.NewServer(box)
	defer boxSrv.Close()
	meteo := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/search" {
			fmt.Fprint(w, `{"results":[{"name":"Oslo","country":"Norway","latitude":59.91,"longitude":10.75}]}`)
			return
		}
		fmt.Fprintf(w, `{"current":{"temperature_2m":%s,"weather_code":0,"is_day":1}}`,
			map[bool]string{true: "7", false: "21"}[r.URL.Query().Get("latitude") == "59.9100"])
	}))
	defer meteo.Close()
	sources.ForecastURL, sources.GeocodingURL = meteo.URL+"/forecast", meteo.URL+"/search"
	t.Setenv("BOX_URL", boxSrv.URL)

	dir := t.TempDir()
	store, _ := settings.Open(dir)
	s := store.Get()
	s.Feeds = nil // no network for feeds here
	store.Set(s)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	svc := service.New(store, dir, "")
	go svc.Run(ctx)
	page := httptest.NewServer(Handler(ctx, svc))
	defer page.Close()

	call := func(method, path, body string, header ...string) (int, string) {
		req, _ := http.NewRequest(method, page.URL+path, strings.NewReader(body))
		if body != "" {
			req.Header.Set("Content-Type", "application/json")
		}
		for i := 0; i+1 < len(header); i += 2 {
			req.Header.Set(header[i], header[i+1])
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		data, _ := io.ReadAll(resp.Body)
		return resp.StatusCode, string(data)
	}
	waitFor := func(want string) {
		t.Helper()
		for range 100 {
			for _, got := range box.texts() {
				if got == want {
					return
				}
			}
			time.Sleep(20 * time.Millisecond)
		}
		t.Fatalf("the box never got %q: %v", want, box.texts())
	}

	if code, body := call("GET", "/", ""); code != 200 || !strings.Contains(body, "<title>Box essentials</title>") {
		t.Fatalf("page: %d", code)
	}
	waitFor("Stockholm: 21° Sunny")

	_, places := call("GET", "/api/places?q=oslo", "")
	var found []sources.Place
	json.Unmarshal([]byte(places), &found)
	if len(found) != 1 || found[0].Name != "Oslo" {
		t.Fatalf("places: %s", places)
	}

	// Change the place the way the page does: the settings back, edited.
	_, raw := call("GET", "/api/settings", "")
	var view map[string]any
	json.Unmarshal([]byte(raw), &view)
	if view["default_box_url"] != boxSrv.URL || len(view["visuals"].([]any)) == 0 {
		t.Fatalf("settings: %s", raw)
	}
	weather := view["weather"].(map[string]any)
	weather["place"] = found[0]
	edited, _ := json.Marshal(map[string]any{"box_url": view["box_url"], "weather": weather, "feeds": view["feeds"]})
	if code, body := call("PUT", "/api/settings", string(edited)); code != 200 {
		t.Fatalf("save: %d %s", code, body)
	}
	waitFor("Oslo: 7° Sunny")
	if reopened, _ := settings.Open(dir); reopened.Get().Weather.Place.Name != "Oslo" {
		t.Fatal("not kept on disk")
	}

	if code, body := call("PUT", "/api/settings", `{"weather":{"place":{"name":""}},"feeds":[]}`); code != 422 ||
		!strings.Contains(body, "needs a place") {
		t.Fatalf("invalid: %d %s", code, body)
	}
	if code, _ := call("POST", "/api/box/send", `{"title":"Hi","text":"from the web"}`); code != 200 {
		t.Fatalf("send: %d", code)
	}
	waitFor("Hi: from the web")
	if code, _ := call("POST", "/api/box/send", `{"text":"evil"}`, "Sec-Fetch-Site", "cross-site"); code != 403 {
		t.Fatalf("a page on another site must not drive the box: %d", code)
	}

	_, ov := call("GET", "/api/overview", "")
	var overview struct {
		Online  bool                      `json:"online"`
		Sources map[string]sources.Report `json:"sources"`
	}
	json.Unmarshal([]byte(ov), &overview)
	if !overview.Online || !overview.Sources["weather"].Sent || overview.Sources["weather"].Shown != "Oslo: 7° Sunny" {
		t.Fatalf("overview: %s", ov)
	}
}
