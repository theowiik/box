// Package web is box-essentials' settings page: where the weather is for,
// which feeds to follow, where the box is; and a look at what the box shows.
// One page (index.html) over a small JSON API on the same server.
package web

import (
	"context"
	_ "embed"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"strings"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
	"github.com/theowiik/box/connectors/box-essentials/internal/service"
	"github.com/theowiik/box/connectors/box-essentials/internal/settings"
	"github.com/theowiik/box/connectors/box-essentials/internal/sources"
	"github.com/theowiik/box/connectors/box-essentials/internal/visuals"
)

//go:embed index.html
var page []byte

// Handler serves the page and its API. ctx bounds the sources it restarts.
func Handler(ctx context.Context, svc *service.Service) http.Handler {
	h := &handler{ctx: ctx, svc: svc, http: &http.Client{Timeout: 10 * time.Second}}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Cache-Control", "no-cache")
		w.Write(page)
	})
	mux.HandleFunc("GET /api/settings", h.getSettings)
	mux.HandleFunc("PUT /api/settings", h.putSettings)
	mux.HandleFunc("GET /api/places", h.places)
	mux.HandleFunc("GET /api/overview", h.overview)
	mux.HandleFunc("POST /api/box/send", h.send)
	mux.HandleFunc("PATCH /api/box/display", h.display)
	mux.HandleFunc("POST /api/box/clear", h.clear)
	return sameOrigin(mux)
}

type handler struct {
	ctx  context.Context
	svc  *service.Service
	http *http.Client
}

// settingsView is the settings plus what the page needs to edit them.
type settingsView struct {
	settings.Settings
	DefaultBoxURL string   `json:"default_box_url"`
	Visuals       []string `json:"visuals"`
	Priorities    []string `json:"priorities"`
}

func (h *handler) view(s settings.Settings) settingsView {
	return settingsView{s, client.New("", "").URL, visuals.Names(), client.Priorities}
}

func (h *handler) getSettings(w http.ResponseWriter, r *http.Request) {
	reply(w, http.StatusOK, h.view(h.svc.Store.Get()))
}

func (h *handler) putSettings(w http.ResponseWriter, r *http.Request) {
	// A page that does not know a section keeps what is set.
	s := settings.Settings{Idle: h.svc.Store.Get().Idle}
	if !decode(w, r, &s) {
		return
	}
	saved, err := h.svc.Apply(h.ctx, s)
	if err != nil {
		fail(w, http.StatusUnprocessableEntity, err)
		return
	}
	slog.Info("settings changed", "from", r.RemoteAddr)
	reply(w, http.StatusOK, h.view(saved))
}

func (h *handler) places(w http.ResponseWriter, r *http.Request) {
	q := strings.TrimSpace(r.URL.Query().Get("q"))
	if len([]rune(q)) < 2 {
		reply(w, http.StatusOK, []sources.Place{})
		return
	}
	places, err := sources.FindPlaces(r.Context(), h.http, q)
	if err != nil {
		fail(w, http.StatusBadGateway, errors.New("could not look places up: "+err.Error()))
		return
	}
	reply(w, http.StatusOK, places)
}

// overview is the box (reachable? what it shows) and how each source's last
// poll went.
func (h *handler) overview(w http.ResponseWriter, r *http.Request) {
	box := h.svc.Box()
	out := struct {
		BoxURL  string                    `json:"box_url"`
		Online  bool                      `json:"online"`
		Error   string                    `json:"error,omitempty"`
		Status  json.RawMessage           `json:"status,omitempty"`
		Sources map[string]sources.Report `json:"sources"`
	}{BoxURL: box.URL, Sources: h.svc.Reports()}
	if status, err := box.Status(); err != nil {
		out.Error = err.Error()
	} else {
		out.Online, out.Status = true, status
	}
	reply(w, http.StatusOK, out)
}

func (h *handler) send(w http.ResponseWriter, r *http.Request) {
	var in struct{ Title, Text, Visual, Priority string }
	if !decode(w, r, &in) {
		return
	}
	var e client.Event
	switch {
	case in.Visual != "":
		e = client.Visual("web", in.Visual)
	case strings.TrimSpace(in.Text) != "":
		e = client.Text("web", strings.TrimSpace(in.Title), strings.TrimSpace(in.Text))
	default:
		fail(w, http.StatusUnprocessableEntity, errors.New("write something to send"))
		return
	}
	e.Priority = in.Priority
	accepted, err := h.svc.Box().Send(e)
	if err != nil {
		boxFailed(w, err)
		return
	}
	reply(w, http.StatusOK, accepted)
}

func (h *handler) display(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Brightness *float64 `json:"brightness"`
		Style      string   `json:"style"`
	}
	if !decode(w, r, &in) {
		return
	}
	d, err := h.svc.Box().Settings(in.Brightness, in.Style)
	if err != nil {
		boxFailed(w, err)
		return
	}
	reply(w, http.StatusOK, d)
}

func (h *handler) clear(w http.ResponseWriter, r *http.Request) {
	var in struct{ Ambient bool }
	if !decode(w, r, &in) {
		return
	}
	if err := h.svc.Box().Clear(in.Ambient); err != nil {
		boxFailed(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

// sameOrigin refuses writes from other sites' pages: the API has no login,
// so a page elsewhere must not be able to post to it from your browser.
func sameOrigin(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			if site := r.Header.Get("Sec-Fetch-Site"); site != "" && site != "same-origin" && site != "none" {
				fail(w, http.StatusForbidden, errors.New("cross-site request"))
				return
			}
			if !strings.HasPrefix(r.Header.Get("Content-Type"), "application/json") {
				fail(w, http.StatusUnsupportedMediaType, errors.New("send JSON"))
				return
			}
		}
		next.ServeHTTP(w, r)
	})
}

func decode(w http.ResponseWriter, r *http.Request, v any) bool {
	dec := json.NewDecoder(io.LimitReader(r.Body, 1<<20))
	dec.DisallowUnknownFields()
	if err := dec.Decode(v); err != nil {
		fail(w, http.StatusBadRequest, errors.New("bad JSON: "+err.Error()))
		return false
	}
	return true
}

func boxFailed(w http.ResponseWriter, err error) {
	status := http.StatusBadGateway
	if client.IsRefusal(err) {
		status = http.StatusUnprocessableEntity
	}
	fail(w, status, err)
}

func fail(w http.ResponseWriter, status int, err error) {
	reply(w, status, map[string]string{"detail": err.Error()})
}

func reply(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}
