package client

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/visuals"
)

// DefaultURL is where the box answers unless BOX_URL says otherwise: it
// announces itself over mDNS as box.local.
const DefaultURL = "http://box.local"

const (
	timeout = 10 * time.Second
	// How long an address found over mDNS is reused before asking again.
	mdnsCache = 5 * time.Minute
	// Visuals were drawn for small panels: they render at most this many
	// pixels a side and the box scales them up.
	visualMaxSide = 48
)

// Display is what GET /api/display reports.
type Display struct {
	Width      int      `json:"width"`
	Height     int      `json:"height"`
	Shape      string   `json:"shape"`
	Content    []string `json:"content"`
	Styles     []string `json:"styles"`
	Style      string   `json:"style"`
	LEDGrid    int      `json:"led_grid"`
	Brightness float64  `json:"brightness"`
	FPS        int      `json:"fps"`
	Limits     struct {
		TextChars  int `json:"text_chars"`
		TitleChars int `json:"title_chars"`
		ImageBytes int `json:"image_bytes"`
		ImageSide  int `json:"image_side"`
	} `json:"limits"`
}

// Accepted is the box's answer to an event.
type Accepted struct {
	ID       string  `json:"id"`
	Source   string  `json:"source"`
	Priority string  `json:"priority"`
	Duration float64 `json:"duration"`
	Queued   int     `json:"queued"`
}

// Box is a client for one box. Safe for concurrent use.
type Box struct {
	URL   string
	Token string
	http  *http.Client

	mu       sync.Mutex
	display  *Display
	resolved string
	expires  time.Time
}

// New returns a client for url (empty: BOX_URL, else box.local), sending
// token as a bearer token when set (empty: BOX_TOKEN).
func New(url, token string) *Box {
	if url == "" {
		url = os.Getenv("BOX_URL")
	}
	if url == "" {
		url = DefaultURL
	}
	if token == "" {
		token = os.Getenv("BOX_TOKEN")
	}
	return &Box{URL: strings.TrimRight(url, "/"), Token: token, http: &http.Client{Timeout: timeout}}
}

// Send posts an event, rendering a built-in visual into an image first.
func (b *Box) Send(e Event) (Accepted, error) {
	if e.Visual != "" {
		content, err := b.RenderVisual(e.Visual)
		if err != nil {
			return Accepted{}, err
		}
		e.Content = content
	}
	var out Accepted
	err := b.do(http.MethodPost, "/api/events", e, &out)
	return out, err
}

// Status is what GET /api/status reports, kept as raw JSON for display.
func (b *Box) Status() (json.RawMessage, error) {
	var out json.RawMessage
	err := b.do(http.MethodGet, "/api/status", nil, &out)
	return out, err
}

// Display reports what the box can do. Asked once, then cached.
func (b *Box) Display() (Display, error) {
	b.mu.Lock()
	cached := b.display
	b.mu.Unlock()
	if cached != nil {
		return *cached, nil
	}
	var d Display
	if err := b.do(http.MethodGet, "/api/display", nil, &d); err != nil {
		return d, err
	}
	b.mu.Lock()
	b.display = &d
	b.mu.Unlock()
	return d, nil
}

// Settings changes brightness (0..1) and/or style until the box reboots;
// nil leaves a setting alone.
func (b *Box) Settings(brightness *float64, style string) (Display, error) {
	body := map[string]any{}
	if brightness != nil {
		body["brightness"] = *brightness
	}
	if style != "" {
		body["style"] = style
	}
	var d Display
	err := b.do(http.MethodPatch, "/api/display", body, &d)
	return d, err
}

// ClearAmbient empties the idle rotation; what is on screen stays.
func (b *Box) ClearAmbient() error { return b.do(http.MethodDelete, "/api/ambient", nil, nil) }

// Clear drops the queue and what is showing; with ambient, also the idle rotation.
func (b *Box) Clear(ambient bool) error {
	if err := b.do(http.MethodDelete, "/api/events", nil, nil); err != nil || !ambient {
		return err
	}
	return b.do(http.MethodDelete, "/api/ambient", nil, nil)
}

// RenderVisual renders a built-in visual as image content the box accepts:
// at a whole fraction of the screen, at most visualMaxSide pixels a side,
// and within one upload.
func (b *Box) RenderVisual(name string) (Content, error) {
	v, ok := visuals.All[name]
	if !ok {
		return Content{}, fmt.Errorf("unknown visual %q; try %s", name, strings.Join(visuals.Names(), ", "))
	}
	d, err := b.Display()
	if err != nil {
		return Content{}, err
	}
	limit := d.Limits.ImageBytes
	if limit == 0 {
		limit = 2 << 20
	}
	side := max(d.Width, d.Height)
	step := max(1, (side+visualMaxSide-1)/visualMaxSide)
	for {
		w, h := max(1, d.Width/step), max(1, d.Height/step)
		frames := v.Render(w, h)
		if len(frames) <= limit || (w == 1 && h == 1) {
			loop := v.Loop
			return Content{
				Type: "image", Width: w, Height: h, Data: frames,
				FPS: v.FPS, Loop: &loop, Scaling: "pixelated",
			}, nil
		}
		step++
	}
}

// Error is a refusal from the box, with its reason.
type Error struct {
	Status int
	Detail string
}

func (e *Error) Error() string {
	if e.Status == http.StatusUnauthorized {
		return "the box wants a token: set BOX_TOKEN"
	}
	return fmt.Sprintf("the box refused it (%d): %s", e.Status, e.Detail)
}

func (b *Box) do(method, path string, body, out any) error {
	var reader io.Reader
	if body != nil {
		data, err := json.Marshal(body)
		if err != nil {
			return err
		}
		reader = bytes.NewReader(data)
	}
	req, err := http.NewRequest(method, b.base()+path, reader)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if b.Token != "" {
		req.Header.Set("Authorization", "Bearer "+b.Token)
	}
	resp, err := b.http.Do(req)
	if err != nil {
		b.forget() // the box may have a new address: look it up again next time
		return fmt.Errorf("could not reach the box at %s: %w (is it on and on Wi-Fi? "+
			"set BOX_URL if box.local does not resolve)", b.URL, err)
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return err
	}
	if resp.StatusCode >= 300 {
		var reason struct {
			Detail string `json:"detail"`
		}
		if json.Unmarshal(data, &reason) != nil || reason.Detail == "" {
			reason.Detail = strings.TrimSpace(string(data))
		}
		return &Error{Status: resp.StatusCode, Detail: reason.Detail}
	}
	if out == nil {
		return nil
	}
	return json.Unmarshal(data, out)
}

// base is the box's URL, with a .local name the system cannot resolve (in
// Docker, say) replaced by the address mDNS gives for it.
func (b *Box) base() string {
	u, err := url.Parse(b.URL)
	if err != nil || !strings.HasSuffix(u.Hostname(), ".local") {
		return b.URL
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.resolved == "" || time.Now().After(b.expires) {
		if _, err := net.LookupHost(u.Hostname()); err == nil {
			return b.URL // the system resolves it: avahi, Bonjour, nss-mdns
		}
		addr, err := Resolve(u.Hostname(), 2*time.Second)
		if err != nil {
			return b.URL // let the request fail with the usual message
		}
		b.resolved, b.expires = addr, time.Now().Add(mdnsCache)
	}
	host := b.resolved
	if port := u.Port(); port != "" {
		host = net.JoinHostPort(host, port)
	}
	u.Host = host
	return strings.TrimRight(u.String(), "/")
}

func (b *Box) forget() {
	b.mu.Lock()
	b.resolved = ""
	b.mu.Unlock()
}

// IsRefusal reports whether err is the box refusing a request (as opposed to
// not being reachable).
func IsRefusal(err error) bool {
	var e *Error
	return errors.As(err, &e)
}
