package client

import (
	"encoding/binary"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
)

// fakeBox is the box's HTTP API, enough to test the client.
type fakeBox struct {
	mu     sync.Mutex
	events []map[string]any
	token  string
	status int // answer POST /api/events with this, when set
	width  int
	limit  int
}

func (f *fakeBox) serve(t *testing.T) *httptest.Server {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if f.token != "" && r.Method != http.MethodGet && r.Header.Get("Authorization") != "Bearer "+f.token {
			w.WriteHeader(401)
			io.WriteString(w, `{"detail":"Bearer token required"}`)
			return
		}
		switch {
		case r.URL.Path == "/api/display" && r.Method == http.MethodGet:
			json.NewEncoder(w).Encode(map[string]any{
				"width": f.width, "height": f.width,
				"limits": map[string]any{"image_bytes": f.limit},
			})
		case r.URL.Path == "/api/events" && r.Method == http.MethodPost:
			if f.status != 0 {
				w.WriteHeader(f.status)
				io.WriteString(w, `{"detail":"nope"}`)
				return
			}
			var e map[string]any
			json.NewDecoder(r.Body).Decode(&e)
			f.mu.Lock()
			f.events = append(f.events, e)
			f.mu.Unlock()
			io.WriteString(w, `{"id":"abc","source":"me","priority":"normal","duration":8,"queued":0}`)
		default:
			w.WriteHeader(404)
		}
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestURLDefaultsAndEnv(t *testing.T) {
	t.Setenv("BOX_URL", "")
	if got := New("", "").URL; got != DefaultURL {
		t.Fatalf("default %q", got)
	}
	t.Setenv("BOX_URL", "http://192.168.0.228/")
	if got := New("", "").URL; got != "http://192.168.0.228" {
		t.Fatalf("env %q", got)
	}
	if got := New("http://other:8080/", "").URL; got != "http://other:8080" {
		t.Fatalf("explicit %q", got)
	}
}

func TestSendText(t *testing.T) {
	fake := &fakeBox{width: 480}
	b := New(fake.serve(t).URL, "")
	e := Text("me", "Title", "hello")
	e.Priority = "alert"
	if _, err := b.Send(e); err != nil {
		t.Fatal(err)
	}
	got := fake.events[0]
	content := got["content"].(map[string]any)
	if got["priority"] != "alert" || content["type"] != "text" || content["title"] != "Title" ||
		content["text"] != "hello" {
		t.Fatalf("sent %v", got)
	}
	if _, ok := content["width"]; ok {
		t.Fatal("empty image fields must be left out")
	}
}

func TestLongTextIsTrimmed(t *testing.T) {
	e := Text("me", strings.Repeat("t", 100), strings.Repeat("x", 600))
	if n := len([]rune(e.Content.Text)); n != TextMax {
		t.Fatalf("text %d chars", n)
	}
	if n := len([]rune(e.Content.Title)); n != TitleMax {
		t.Fatalf("title %d chars", n)
	}
}

func TestVisualsAreRenderedHereSmallEnough(t *testing.T) {
	fake := &fakeBox{width: 480, limit: 4 << 20}
	b := New(fake.serve(t).URL, "")
	if _, err := b.Send(Visual("me", "gradient")); err != nil {
		t.Fatal(err)
	}
	c := fake.events[0]["content"].(map[string]any)
	if c["type"] != "image" || c["width"] != 48.0 || c["height"] != 48.0 || c["scaling"] != "pixelated" {
		t.Fatalf("visual sent as %v", c["type"])
	}
	// A tight limit shrinks it further.
	fake2 := &fakeBox{width: 48, limit: 120 * 24 * 24 * 3}
	content, err := New(fake2.serve(t).URL, "").RenderVisual("gradient")
	if err != nil || content.Width != 24 || len(content.Data) > fake2.limit {
		t.Fatalf("got %dx%d, %v", content.Width, content.Height, err)
	}
}

func TestRefusalsAndTokens(t *testing.T) {
	fake := &fakeBox{width: 480, token: "s3cret"}
	srv := fake.serve(t)
	if _, err := New(srv.URL, "x").Send(Text("me", "", "x")); err == nil ||
		!strings.Contains(err.Error(), "token") {
		t.Fatalf("want token error, got %v", err)
	}
	if _, err := New(srv.URL, "s3cret").Send(Text("me", "", "x")); err != nil {
		t.Fatal(err)
	}
	fake.status = 422
	_, err := New(srv.URL, "s3cret").Send(Text("me", "", "x"))
	if !IsRefusal(err) || !strings.Contains(err.Error(), "422") || !strings.Contains(err.Error(), "nope") {
		t.Fatalf("got %v", err)
	}
}

func TestUnreachableBox(t *testing.T) {
	_, err := New("http://127.0.0.1:9", "").Send(Text("me", "", "x"))
	if err == nil || IsRefusal(err) || !strings.Contains(err.Error(), "could not reach") {
		t.Fatalf("got %v", err)
	}
}

// answer is an mDNS response like the box's: the question echoed, then an A record.
func answer(name, addr string, compress bool) []byte {
	question := Query(name)[12:]
	out := make([]byte, 12)
	binary.BigEndian.PutUint16(out[2:], 0x8400)
	binary.BigEndian.PutUint16(out[4:], 1)
	binary.BigEndian.PutUint16(out[6:], 1)
	out = append(out, question...)
	if compress {
		out = append(out, 0xC0, 0x0C)
	} else {
		out = append(out, question[:len(question)-4]...)
	}
	rec := make([]byte, 10)
	binary.BigEndian.PutUint16(rec[0:], 1)
	binary.BigEndian.PutUint16(rec[2:], 0x8001)
	binary.BigEndian.PutUint16(rec[8:], 4)
	out = append(out, rec...)
	return append(out, net.ParseIP(addr).To4()...)
}

func TestMDNS(t *testing.T) {
	if q := Query("box.local"); string(q[12:]) != "\x03box\x05local\x00\x00\x01\x00\x01" {
		t.Fatalf("query %q", q)
	}
	if a, ok := ParseAddress(answer("box.local", "192.168.0.228", true), "box.local"); !ok || a != "192.168.0.228" {
		t.Fatalf("got %q %v", a, ok)
	}
	if a, ok := ParseAddress(answer("box.local", "10.0.0.5", false), "BOX.local."); !ok || a != "10.0.0.5" {
		t.Fatalf("got %q %v", a, ok)
	}
	if _, ok := ParseAddress(answer("printer.local", "10.0.0.9", true), "box.local"); ok {
		t.Fatal("matched another name")
	}
	if _, ok := ParseAddress(Query("box.local"), "box.local"); ok {
		t.Fatal("a question is not an answer")
	}
	loop := append(make([]byte, 12), 0xC0, 0x0C, 0xC0, 0x0C)
	loop[2], loop[7] = 0x84, 1
	if _, ok := ParseAddress(loop, "box.local"); ok {
		t.Fatal("junk parsed")
	}
}
