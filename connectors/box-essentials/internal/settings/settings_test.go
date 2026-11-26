package settings

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDefaultsAreValid(t *testing.T) {
	s := Default()
	if err := s.Validate(); err != nil {
		t.Fatal(err)
	}
}

func TestValidateNamesEveryProblem(t *testing.T) {
	s := Default()
	s.BoxURL = "box.local"
	s.Weather.Place.Name = " "
	s.Weather.EveryMinutes = 0
	s.Feeds = append(s.Feeds,
		Feed{Name: "essr", URL: "https://x.se/feed", EveryMinutes: 5},
		Feed{Name: "Bad Name", URL: "ftp://x", Show: "fireworks", Priority: "urgent"},
		Feed{Name: "weather", URL: "https://x.se/feed", EveryMinutes: 5},
	)
	err := s.Validate()
	if err == nil {
		t.Fatal("invalid settings passed")
	}
	for _, want := range []string{"box's address", "needs a place", "minute apart", "essr: two sources",
		"Bad Name: the name", "Bad Name: the feed's address", "Bad Name: show", "Bad Name: the priority",
		"weather: two sources"} {
		if !strings.Contains(err.Error(), want) {
			t.Errorf("missing %q in: %v", want, err)
		}
	}
}

func TestValidateTidies(t *testing.T) {
	s := Default()
	s.BoxURL = " http://192.168.0.228/ "
	s.Feeds[0].Show = ""
	s.Feeds[0].URL = " https://example.com/feed.xml "
	if err := s.Validate(); err != nil {
		t.Fatal(err)
	}
	if s.BoxURL != "http://192.168.0.228" || s.Feeds[0].Show != "title" || s.Feeds[0].URL != "https://example.com/feed.xml" {
		t.Fatalf("%+v", s)
	}
}

func TestStoreKeepsSettings(t *testing.T) {
	dir := t.TempDir()
	st, err := Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	if st.Get().Weather.Place.Name != "Stockholm" {
		t.Fatal("a fresh install starts from the defaults")
	}
	s := st.Get()
	s.Weather.Place.Name = "Oslo"
	s.Feeds = nil
	if _, err := st.Set(s); err != nil {
		t.Fatal(err)
	}
	bad := st.Get()
	bad.Weather.EveryMinutes = 0
	if _, err := st.Set(bad); err == nil {
		t.Fatal("invalid settings were saved")
	}
	again, err := Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	got := again.Get()
	if got.Weather.Place.Name != "Oslo" || got.Weather.EveryMinutes != 10 || got.Feeds == nil || len(got.Feeds) != 0 {
		t.Fatalf("%+v", got)
	}
	os.WriteFile(filepath.Join(dir, "settings.json"), []byte("{oops"), 0o644)
	if _, err := Open(dir); err == nil {
		t.Fatal("a corrupt file must stop the service, not reset the settings")
	}
}
