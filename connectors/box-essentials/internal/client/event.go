// Package client talks to the box: events, its status and its settings.
package client

// An Event is what the box shows: content, from a source, at a priority.
// See the repo README for the API.
type Event struct {
	Source   string  `json:"source,omitempty"`
	Priority string  `json:"priority,omitempty"` // ambient, normal, alert, alarm
	Duration float64 `json:"duration,omitempty"` // seconds; 0 for the priority's default
	Style    string  `json:"style,omitempty"`    // smooth or led; empty for the box's
	Content  Content `json:"content"`

	// Visual names a built-in visual for Send to render into Content. It is
	// never sent: the box only knows images.
	Visual string `json:"-"`
}

// Content is one of: text, image, color, clear, glance.
type Content struct {
	Type string `json:"type"`

	// text
	Title string `json:"title,omitempty"`
	Text  string `json:"text,omitempty"`
	Align string `json:"align,omitempty"` // center or left
	// text and color
	Color string `json:"color,omitempty"` // #RRGGBB; empty lets the box pick

	// glance: the time, the weather and a smiley; Color tints it
	Date      string `json:"date,omitempty"`
	Time      string `json:"time,omitempty"`
	Temp      string `json:"temp,omitempty"`
	Condition string `json:"condition,omitempty"`
	Sky       string `json:"sky,omitempty"`  // sun, moon, partly, cloud, fog, rain, snow, storm
	Mood      string `json:"mood,omitempty"` // happy, good, meh, sad

	// image: Data is width*height*3 RGB bytes per frame (base64 in JSON)
	Width   int     `json:"width,omitempty"`
	Height  int     `json:"height,omitempty"`
	FPS     float64 `json:"fps,omitempty"`
	Loop    *bool   `json:"loop,omitempty"`
	Scaling string  `json:"scaling,omitempty"` // pixelated or smooth
	Data    []byte  `json:"data,omitempty"`
}

// Priorities, most patient first.
var Priorities = []string{"ambient", "normal", "alert", "alarm"}

// TextMax and TitleMax are the box's limits, in characters.
const (
	TextMax  = 512
	TitleMax = 80
)

// Text is a text event. Over-long text is trimmed to what the box accepts.
func Text(source, title, text string) Event {
	return Event{Source: source, Content: Content{
		Type: "text", Title: trim(title, TitleMax), Text: trim(text, TextMax),
	}}
}

// Visual is an event playing a built-in visual (see package visuals).
func Visual(source, name string) Event {
	return Event{Source: source, Visual: name}
}

// Image is an animation of RGB frames.
func Image(source string, width, height int, frames []byte, fps float64, loop bool, scaling string) Event {
	return Event{Source: source, Content: Content{
		Type: "image", Width: width, Height: height, Data: frames,
		FPS: fps, Loop: &loop, Scaling: scaling,
	}}
}

func trim(s string, max int) string {
	r := []rune(s)
	if len(r) <= max {
		return s
	}
	return string(r[:max-1]) + "…"
}
