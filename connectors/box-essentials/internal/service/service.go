// Package service is the always-on part of box-essentials: it runs the
// sources the settings ask for, and starts them over when the settings
// change.
package service

import (
	"context"
	"log/slog"
	"strings"
	"sync"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/client"
	"github.com/theowiik/box/connectors/box-essentials/internal/settings"
	"github.com/theowiik/box/connectors/box-essentials/internal/sources"
)

type Service struct {
	Store    *settings.Store
	StateDir string // feed history
	Token    string // the box's bearer token, if it wants one

	restarting sync.Mutex // one restart at a time
	mu         sync.Mutex // the fields below
	box        *client.Box
	reports    map[string]sources.Report
	stop       context.CancelFunc
	done       chan struct{}
}

func New(store *settings.Store, stateDir, token string) *Service {
	return &Service{Store: store, StateDir: stateDir, Token: token, reports: map[string]sources.Report{}}
}

// Run runs the sources until ctx ends.
func (s *Service) Run(ctx context.Context) {
	s.restart(ctx, s.Store.Get())
	<-ctx.Done()
	s.mu.Lock()
	done := s.done
	s.mu.Unlock()
	<-done
}

// Apply saves new settings and starts the sources over with them: each
// polls at once, so a new place shows its weather right away.
func (s *Service) Apply(ctx context.Context, next settings.Settings) (settings.Settings, error) {
	saved, err := s.Store.Set(next)
	if err != nil {
		return settings.Settings{}, err
	}
	s.restart(ctx, saved)
	return saved, nil
}

// Box is the client for the box the settings name.
func (s *Service) Box() *client.Box {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.box
}

// Reports is how each running source's last poll went, by source.
func (s *Service) Reports() map[string]sources.Report {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make(map[string]sources.Report, len(s.reports))
	for k, v := range s.reports {
		out[k] = v
	}
	return out
}

func (s *Service) restart(ctx context.Context, set settings.Settings) {
	s.restarting.Lock()
	defer s.restarting.Unlock()
	// Stop the old sources without holding mu: they take it to report.
	s.mu.Lock()
	stop, done := s.stop, s.done
	s.mu.Unlock()
	if stop != nil {
		stop()
		<-done
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	if s.box == nil || s.box.URL != effectiveURL(set.BoxURL) {
		s.box = client.New(set.BoxURL, s.Token)
	}
	srcs := Sources(set, s.StateDir)
	s.reports = map[string]sources.Report{}
	run, stop := context.WithCancel(ctx)
	done = make(chan struct{})
	s.stop, s.done = stop, done
	names := make([]string, len(srcs))
	for i, src := range srcs {
		names[i] = src.Name() + " every " + src.Interval().String()
	}
	slog.Info("posting to the box", "url", s.box.URL, "sources", strings.Join(names, ", "))
	box := s.box
	go func() {
		defer close(done)
		sources.Run(run, box, srcs, func(r sources.Report) {
			s.mu.Lock()
			defer s.mu.Unlock()
			if run.Err() == nil { // not a report from sources that were replaced
				s.reports[r.Source] = r
			}
		})
	}()
}

func effectiveURL(u string) string { return client.New(u, "").URL }

// Sources are what the settings turn on.
func Sources(set settings.Settings, stateDir string) []sources.Source {
	var out []sources.Source
	if w := set.Weather; w.Enabled {
		out = append(out, sources.NewWeather(w.Place, w.Fahrenheit, time.Duration(w.EveryMinutes)*time.Minute))
	}
	for _, f := range set.Feeds {
		if !f.Enabled {
			continue
		}
		out = append(out, sources.NewFeed(sources.FeedConfig{
			Name: f.Name, Title: f.Title, URL: f.URL, Show: f.Show, Priority: f.Priority,
			Every: time.Duration(f.EveryMinutes) * time.Minute,
		}, stateDir))
	}
	return out
}
