// Package settings is what box-essentials is set to do: where the box is,
// the weather's place, the feeds. The web page edits it; it is kept as JSON
// in the state directory, so it survives restarts and new images.
package settings

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"sync"
	"time"
	_ "time/tzdata" // the clock's zone, in a distroless image

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
	"github.com/theowiik/box/connectors/box-essentials/internal/sources"
	"github.com/theowiik/box/connectors/box-essentials/internal/visuals"
)

type Settings struct {
	// BoxURL is where the box answers; empty for BOX_URL, else box.local.
	BoxURL  string  `json:"box_url"`
	Weather Weather `json:"weather"`
	Feeds   []Feed  `json:"feeds"`
	Idle    Idle    `json:"idle"`
}

// Idle is what the box rotates through when nothing else is on, besides
// the weather: the time and some visuals, each for Seconds a turn.
type Idle struct {
	Clock    bool     `json:"clock"`
	Glance   bool     `json:"glance"`   // the time, the weather and a smiley on one card
	Timezone string   `json:"timezone"` // IANA, e.g. Europe/Stockholm; empty for the server's
	Visuals  []string `json:"visuals"`
	Seconds  int      `json:"seconds"`
}

type Weather struct {
	Enabled      bool          `json:"enabled"`
	Place        sources.Place `json:"place"`
	Fahrenheit   bool          `json:"fahrenheit"`
	EveryMinutes int           `json:"every_minutes"`
}

type Feed struct {
	Enabled bool `json:"enabled"`
	// Name is the event source: it picks the colour on the box and names the
	// file of items already shown. Letters, digits and dashes.
	Name  string `json:"name"`
	Title string `json:"title"` // shown above the item titles
	URL   string `json:"url"`
	// Show is "title" for the item titles, or a visual to play ("beer").
	Show         string `json:"show"`
	Priority     string `json:"priority"` // empty for normal
	EveryMinutes int    `json:"every_minutes"`
}

// Default is a fresh install: the weather in Stockholm, and En Stor Stark
// Review pouring a pint for each new bar review.
func Default() Settings {
	return Settings{
		Weather: Weather{Enabled: false, EveryMinutes: 10, Place: sources.Place{
			Name: "Stockholm", Region: "Stockholm", Country: "Sweden", Latitude: 59.3293, Longitude: 18.0686}},
		Feeds: []Feed{{Enabled: true, Name: "essr", Title: "En Stor Stark Review",
			URL: "https://enstorstarkreview.se/feed.xml", Show: "beer", EveryMinutes: 5}},
		Idle: Idle{Glance: true, Timezone: "Europe/Stockholm", Visuals: []string{}, Seconds: 12},
	}
}

var feedName = regexp.MustCompile(`^[a-z0-9][a-z0-9-]{0,31}$`)

// Validate checks everything and tidies whitespace; the error is for people.
func (s *Settings) Validate() error {
	var problems []string
	bad := func(format string, a ...any) { problems = append(problems, fmt.Sprintf(format, a...)) }

	s.BoxURL = strings.TrimRight(strings.TrimSpace(s.BoxURL), "/")
	if s.BoxURL != "" {
		if u, err := url.Parse(s.BoxURL); err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
			bad("the box's address must look like http://box.local or http://192.168.0.228")
		}
	}

	w := &s.Weather
	w.Place.Name = strings.TrimSpace(w.Place.Name)
	if w.Place.Name == "" {
		bad("the weather needs a place")
	}
	if w.Place.Latitude < -90 || w.Place.Latitude > 90 || w.Place.Longitude < -180 || w.Place.Longitude > 180 {
		bad("the weather's place is not on Earth")
	}
	if w.EveryMinutes < 1 {
		bad("the weather is checked at least a minute apart")
	}

	names := map[string]bool{"weather": true, "clock": true, "glance": true}
	id := &s.Idle
	id.Timezone = strings.TrimSpace(id.Timezone)
	if _, err := time.LoadLocation(id.Timezone); err != nil {
		bad("the clock's time zone is a name like Europe/Stockholm")
	}
	for _, v := range id.Visuals {
		if _, ok := visuals.All[v]; !ok {
			bad("the idle visuals are some of %s", strings.Join(visuals.Names(), ", "))
			break
		}
	}
	if id.Seconds < 3 || id.Seconds > 600 {
		bad("each idle card shows for 3 to 600 seconds")
	}
	for i := range s.Feeds {
		f := &s.Feeds[i]
		f.Name, f.Title, f.URL = strings.TrimSpace(f.Name), strings.TrimSpace(f.Title), strings.TrimSpace(f.URL)
		label := f.Name
		if label == "" {
			label = fmt.Sprintf("feed %d", i+1)
		}
		if !feedName.MatchString(f.Name) {
			bad("%s: the name is lowercase letters, digits and dashes", label)
		} else if strings.HasPrefix(f.Name, "idle-") {
			bad("%s: names starting idle- are for the idle visuals", label)
		} else if names[f.Name] {
			bad("%s: two sources are called that", label)
		}
		names[f.Name] = true
		if u, err := url.Parse(f.URL); err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
			bad("%s: the feed's address must be an http(s) URL", label)
		}
		if f.Show == "" {
			f.Show = "title"
		}
		if _, ok := visuals.All[f.Show]; f.Show != "title" && !ok {
			bad("%s: show the titles or one of %s", label, strings.Join(visuals.Names(), ", "))
		}
		if f.Priority != "" && !slices.Contains(client.Priorities, f.Priority) {
			bad("%s: the priority is one of %s", label, strings.Join(client.Priorities, ", "))
		}
		if f.EveryMinutes < 1 {
			bad("%s: checked at least a minute apart", label)
		}
	}
	if len(problems) > 0 {
		return errors.New(strings.Join(problems, "; "))
	}
	return nil
}

// Store is the settings on disk. Safe for concurrent use.
type Store struct {
	path string
	mu   sync.Mutex
	cur  Settings
}

// Open reads dir/settings.json, or starts from Default if there is none.
func Open(dir string) (*Store, error) {
	st := &Store{path: filepath.Join(dir, "settings.json"), cur: Default()}
	data, err := os.ReadFile(st.path)
	if errors.Is(err, os.ErrNotExist) {
		return st, nil
	}
	if err != nil {
		return nil, err
	}
	// Settings from before a field existed get its default.
	s := Settings{Idle: Default().Idle}
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("%s: %w", st.path, err)
	}
	if err := s.Validate(); err != nil {
		return nil, fmt.Errorf("%s: %w", st.path, err)
	}
	st.cur = s
	return st, nil
}

func (st *Store) Get() Settings {
	st.mu.Lock()
	defer st.mu.Unlock()
	return clone(st.cur)
}

// Set validates s and writes it.
func (st *Store) Set(s Settings) (Settings, error) {
	s = clone(s)
	if err := s.Validate(); err != nil {
		return Settings{}, err
	}
	st.mu.Lock()
	defer st.mu.Unlock()
	data, _ := json.MarshalIndent(s, "", "  ")
	if err := os.MkdirAll(filepath.Dir(st.path), 0o755); err != nil {
		return Settings{}, err
	}
	// Write then rename, so a crash mid-write cannot leave a corrupt file.
	tmp := st.path + ".tmp"
	if err := os.WriteFile(tmp, append(data, '\n'), 0o644); err != nil {
		return Settings{}, err
	}
	if err := os.Rename(tmp, st.path); err != nil {
		return Settings{}, err
	}
	st.cur = s
	return clone(s), nil
}

func clone(s Settings) Settings {
	s.Feeds = slices.Clone(s.Feeds)
	s.Idle.Visuals = slices.Clone(s.Idle.Visuals)
	if s.Idle.Visuals == nil {
		s.Idle.Visuals = []string{}
	}
	if s.Feeds == nil {
		s.Feeds = []Feed{}
	}
	return s
}
