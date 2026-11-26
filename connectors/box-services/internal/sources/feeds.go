package sources

import (
	"context"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/theowiik/box/connectors/box-services/internal/client"
)

// FeedConfig is one RSS or Atom feed to follow.
type FeedConfig struct {
	Name  string // the event source: picks the colour, coalesces bursts
	Title string // shown above the item titles
	URL   string
	// Show is "title" to show the item titles, or the name of a visual to
	// play instead ("beer").
	Show     string
	Every    time.Duration
	Priority string
}

// Feeds are the feeds this box follows. Adding one is one line.
var Feeds = []FeedConfig{
	// En Stor Stark Review: a new bar review pours a pint.
	{Name: "essr", Title: "En Stor Stark Review", URL: "https://enstorstarkreview.se/feed.xml",
		Show: "beer", Every: 5 * time.Minute},
}

// MaxSeen bounds the remembered items: feeds list only their latest.
const MaxSeen = 500

// Item is one entry of a feed.
type Item struct{ GUID, Title, Link string }

// Parse reads RSS 2.0 or Atom, newest first by convention. Items with no id
// or title are skipped.
func Parse(data []byte) ([]Item, error) {
	var doc struct {
		Items []struct {
			Title string `xml:"title"`
			Link  string `xml:"link"`
			GUID  string `xml:"guid"`
		} `xml:"channel>item"`
		Entries []struct {
			Title string `xml:"title"`
			ID    string `xml:"id"`
			Link  struct {
				Href string `xml:"href,attr"`
			} `xml:"link"`
		} `xml:"entry"`
	}
	if err := xml.Unmarshal(data, &doc); err != nil {
		return nil, err
	}
	var items []Item
	add := func(guid, title, link string) {
		guid, title, link = strings.TrimSpace(guid), strings.TrimSpace(title), strings.TrimSpace(link)
		if guid == "" {
			guid = link
		}
		if guid != "" && title != "" {
			items = append(items, Item{guid, title, link})
		}
	}
	for _, i := range doc.Items {
		add(i.GUID, i.Title, i.Link)
	}
	for _, e := range doc.Entries {
		add(e.ID, e.Title, e.Link.Href)
	}
	return items, nil
}

// Seen are the item ids already shown, kept in a JSON list on disk (the
// format the Python version used, so its history carries over).
type Seen struct {
	path  string
	ids   []string
	Known bool // false until the file exists: the very first poll
}

func LoadSeen(path string) *Seen {
	s := &Seen{path: path}
	data, err := os.ReadFile(path)
	if err != nil {
		return s
	}
	if json.Unmarshal(data, &s.ids) != nil {
		// Treated as a first run: showing nothing beats showing everything.
		slog.Warn("ignoring unreadable state", "path", path)
		s.ids = nil
		return s
	}
	s.Known = true
	return s
}

func (s *Seen) Has(id string) bool { return slices.Contains(s.ids, id) }

func (s *Seen) Add(ids []string) error {
	for _, id := range ids {
		if !s.Has(id) {
			s.ids = append(s.ids, id)
		}
	}
	if len(s.ids) > MaxSeen {
		s.ids = s.ids[len(s.ids)-MaxSeen:]
	}
	s.Known = true
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	data, _ := json.Marshal(s.ids)
	// Write then rename, so a crash mid-write cannot leave a corrupt file.
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}

// Feed polls one feed. The first poll only notes what is there: starting the
// service must not flood the screen with old items.
type Feed struct {
	Config FeedConfig
	HTTP   *http.Client
	seen   *Seen
	etag   string
	items  []Item
}

func NewFeed(c FeedConfig, stateDir string) *Feed {
	if c.Every == 0 {
		c.Every = 5 * time.Minute
	}
	return &Feed{Config: c, HTTP: &http.Client{Timeout: 15 * time.Second},
		seen: LoadSeen(filepath.Join(stateDir, "feed-"+c.Name+".json"))}
}

func (f *Feed) Name() string            { return f.Config.Name }
func (f *Feed) Interval() time.Duration { return f.Config.Every }

// Fetch returns the current items; an unchanged feed (304) answers from the
// last fetch.
func (f *Feed) Fetch(ctx context.Context) ([]Item, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, f.Config.URL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "box-services")
	if f.etag != "" {
		req.Header.Set("If-None-Match", f.etag)
	}
	resp, err := f.HTTP.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotModified {
		return f.items, nil
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s returned %d", f.Config.URL, resp.StatusCode)
	}
	data, err := io.ReadAll(io.LimitReader(resp.Body, 8<<20))
	if err != nil {
		return nil, err
	}
	items, err := Parse(data)
	if err != nil {
		return nil, fmt.Errorf("could not read %s: %w", f.Config.URL, err)
	}
	// Only after a good parse: a half-written feed is fetched in full again.
	f.etag, f.items = resp.Header.Get("ETag"), items
	return items, nil
}

func (f *Feed) Poll(ctx context.Context) (*client.Event, error) {
	items, err := f.Fetch(ctx)
	if err != nil {
		return nil, err
	}
	ids := make([]string, len(items))
	for i, it := range items {
		ids[i] = it.GUID
	}
	if !f.seen.Known {
		slog.Info("following feed", "feed", f.Name(), "items", len(items))
		return nil, f.seen.Add(ids)
	}
	var fresh []Item
	for _, it := range items {
		if !f.seen.Has(it.GUID) {
			fresh = append(fresh, it)
		}
	}
	if len(fresh) == 0 {
		return nil, nil
	}
	newIDs := make([]string, len(fresh))
	for i, it := range fresh {
		newIDs[i] = it.GUID
		slog.Info("new item", "feed", f.Name(), "title", it.Title, "link", it.Link)
	}
	if err := f.seen.Add(newIDs); err != nil {
		return nil, err
	}
	e := f.Event(fresh)
	return &e, nil
}

// Event is one event for everything new: the box keeps one pending event per
// source, so separate events would leave only the last on screen.
func (f *Feed) Event(fresh []Item) client.Event {
	var e client.Event
	if f.Config.Show == "title" || f.Config.Show == "" {
		titles := make([]string, len(fresh))
		for i, it := range fresh {
			titles[len(fresh)-1-i] = it.Title // oldest first
		}
		e = client.Text(f.Name(), f.Config.Title, strings.Join(titles, " · "))
	} else {
		e = client.Visual(f.Name(), f.Config.Show)
	}
	e.Priority = f.Config.Priority
	return e
}
