package sources

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"net/http"
	"net/url"
	"strconv"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
)

// Open-Meteo: free, no key, and it updates about every 15 minutes.
var (
	ForecastURL  = "https://api.open-meteo.com/v1/forecast"
	GeocodingURL = "https://geocoding-api.open-meteo.com/v1/search"
)

// Place is somewhere the weather is for.
type Place struct {
	Name      string  `json:"name"`
	Region    string  `json:"region,omitempty"` // county, state, …
	Country   string  `json:"country,omitempty"`
	Latitude  float64 `json:"latitude"`
	Longitude float64 `json:"longitude"`
}

// Reading is the weather now.
type Reading struct {
	Temp      float64
	Condition string
	Color     string
}

// Weather is the current weather at a place, as an ambient card in the idle
// rotation that is refreshed in place.
type Weather struct {
	Place      Place
	Fahrenheit bool
	Dwell      time.Duration // seconds on screen per turn in the rotation
	Every      time.Duration
	HTTP       *http.Client
}

func NewWeather(p Place, fahrenheit bool, every time.Duration) *Weather {
	if every <= 0 {
		every = 10 * time.Minute
	}
	return &Weather{Place: p, Fahrenheit: fahrenheit, Dwell: 12 * time.Second, Every: every,
		HTTP: &http.Client{Timeout: 15 * time.Second}}
}

func (w *Weather) Name() string            { return "weather" }
func (w *Weather) Interval() time.Duration { return w.Every }

// Get asks Open-Meteo for the weather now.
func (w *Weather) Get(ctx context.Context) (Reading, error) {
	q := url.Values{
		"latitude":  {strconv.FormatFloat(w.Place.Latitude, 'f', 4, 64)},
		"longitude": {strconv.FormatFloat(w.Place.Longitude, 'f', 4, 64)},
		"current":   {"temperature_2m,weather_code,is_day"},
	}
	if w.Fahrenheit {
		q.Set("temperature_unit", "fahrenheit")
	}
	var out struct {
		Current struct {
			Temp  *float64 `json:"temperature_2m"`
			Code  *int     `json:"weather_code"`
			IsDay int      `json:"is_day"`
		} `json:"current"`
	}
	if err := getJSON(ctx, w.HTTP, ForecastURL+"?"+q.Encode(), &out); err != nil {
		return Reading{}, err
	}
	if out.Current.Temp == nil || out.Current.Code == nil {
		return Reading{}, fmt.Errorf("the forecast had no current weather")
	}
	label, color := Condition(*out.Current.Code, out.Current.IsDay == 1)
	return Reading{Temp: *out.Current.Temp, Condition: label, Color: color}, nil
}

func (w *Weather) Poll(ctx context.Context) (*client.Event, error) {
	r, err := w.Get(ctx)
	if err != nil {
		return nil, err
	}
	e := client.Text(w.Name(), w.Place.Name, fmt.Sprintf("%d° %s", int(math.Round(r.Temp)), r.Condition))
	e.Priority = "ambient"
	e.Duration = w.Dwell.Seconds()
	e.Content.Color = r.Color
	return &e, nil
}

// Condition names a WMO weather code (what Open-Meteo reports) and picks the
// card's colour.
func Condition(code int, day bool) (label, color string) {
	switch {
	case code == 0 && day:
		return "Sunny", "#FFD54F"
	case code == 0:
		return "Clear", "#64B5F6"
	case code == 1:
		return "Mostly clear", "#90CAF9"
	case code == 2:
		return "Partly cloudy", "#B0BEC5"
	case code == 3:
		return "Cloudy", "#9E9E9E"
	case code == 45 || code == 48:
		return "Foggy", "#78909C"
	case code >= 51 && code <= 57:
		return "Drizzle", "#81D4FA"
	case code >= 61 && code <= 67, code >= 80 && code <= 82:
		return "Rainy", "#4FC3F7"
	case code >= 71 && code <= 77, code == 85 || code == 86:
		return "Snow", "#E0E0E0"
	case code >= 95:
		return "Storm", "#FF7043"
	}
	return "Unknown", "#9E9E9E"
}

// FindPlaces looks a place up by name, best match first.
func FindPlaces(ctx context.Context, h *http.Client, name string) ([]Place, error) {
	q := url.Values{"name": {name}, "count": {"8"}, "language": {"en"}, "format": {"json"}}
	var out struct {
		Results []struct {
			Name      string  `json:"name"`
			Admin1    string  `json:"admin1"`
			Country   string  `json:"country"`
			Latitude  float64 `json:"latitude"`
			Longitude float64 `json:"longitude"`
		} `json:"results"`
	}
	if err := getJSON(ctx, h, GeocodingURL+"?"+q.Encode(), &out); err != nil {
		return nil, err
	}
	places := make([]Place, 0, len(out.Results))
	for _, r := range out.Results {
		places = append(places, Place{r.Name, r.Admin1, r.Country, r.Latitude, r.Longitude})
	}
	return places, nil
}

func getJSON(ctx context.Context, h *http.Client, u string, out any) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "box-essentials")
	resp, err := h.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("%s returned %d", req.URL.Host, resp.StatusCode)
	}
	return json.NewDecoder(resp.Body).Decode(out)
}
