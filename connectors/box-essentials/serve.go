package main

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/theowiik/box/connectors/box-essentials/internal/service"
	"github.com/theowiik/box/connectors/box-essentials/internal/settings"
	"github.com/theowiik/box/connectors/box-essentials/internal/web"
)

// serve is the always-on part: the sources the settings ask for, and the
// settings page, until stopped.
func serve(listen, url, token string) error {
	if url != "" {
		// What the settings page's "empty" means for this run.
		os.Setenv("BOX_URL", url)
	}
	dir := stateDir()
	store, err := settings.Open(dir)
	if err != nil {
		return err
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	svc := service.New(store, dir, token)
	srv := &http.Server{Addr: listen, Handler: web.Handler(ctx, svc), ReadHeaderTimeout: 10 * time.Second}
	failed := make(chan error, 1)
	go func() {
		slog.Info("settings page", "addr", listen)
		if err := srv.ListenAndServe(); !errors.Is(err, http.ErrServerClosed) {
			failed <- err
			stop()
		}
	}()
	svc.Run(ctx)

	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	srv.Shutdown(shutdown)
	select {
	case err := <-failed:
		return err
	default:
		return nil
	}
}
