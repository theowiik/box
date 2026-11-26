// box-services: everything that talks to the box and stays off it.
//
//	box-services run                   weather and feeds, until stopped
//	box-services send [flags] TEXT     show some text now
//	box-services visual [flags] NAME   play a built-in visual (rainbow, life, gradient, beer)
//	box-services gif [flags] FILE      play a GIF
//	box-services status                what is on screen
//	box-services display [--brightness 0.6] [--style led]   what the box can do; change settings
//	box-services clear [--ambient]     drop the queue (and the idle rotation)
//
// Configuration is BOX_* environment variables, or a .env at the repo root:
// BOX_URL (default http://box.local), BOX_TOKEN, BOX_CITY, BOX_STATE_DIR and
// BOX_FEEDS=0 to leave the feeds out.
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"path/filepath"
	"slices"
	"strings"
	"syscall"

	"github.com/theowiik/box/connectors/box-services/internal/client"
	"github.com/theowiik/box/connectors/box-services/internal/gifs"
	"github.com/theowiik/box/connectors/box-services/internal/sources"
	"github.com/theowiik/box/connectors/box-services/internal/visuals"
)

func main() {
	loadDotenv()
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, "box-services:", err)
		os.Exit(1)
	}
}

func usage() error {
	return errors.New("usage: box-services run | send TEXT | visual NAME | gif FILE | status | display | clear\n" +
		"  add --help to a command for its options")
}

func run(args []string) error {
	if len(args) == 0 {
		return usage()
	}
	cmd, args := args[0], args[1:]
	fs := flag.NewFlagSet(cmd, flag.ContinueOnError)
	url := fs.String("url", "", "the box (default: BOX_URL, else http://box.local)")
	token := fs.String("token", "", "bearer token (default: BOX_TOKEN)")
	box := func() *client.Box { return client.New(*url, *token) }

	// Shared by the one-off commands.
	source := fs.String("source", "cli", "who it is from")
	priority := fs.String("priority", "normal", "ambient, normal, alert or alarm")
	duration := fs.Float64("duration", 0, "seconds on screen (default: the priority's)")
	style := fs.String("style", "", "smooth or led (default: the box's)")
	send := func(e client.Event) error {
		if !slices.Contains(client.Priorities, *priority) {
			return fmt.Errorf("--priority must be one of %s", strings.Join(client.Priorities, ", "))
		}
		e.Priority, e.Duration, e.Style = *priority, *duration, *style
		a, err := box().Send(e)
		if err == nil {
			fmt.Printf("sent: %s for %gs, %d queued\n", a.Priority, a.Duration, a.Queued)
		}
		return err
	}

	switch cmd {
	case "run":
		if _, err := parse(fs, args, 0); err != nil {
			return err
		}
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		b := box()
		srcs := []sources.Source{sources.NewWeather(env("BOX_CITY", "Stockholm"))}
		if os.Getenv("BOX_FEEDS") != "0" {
			for _, f := range sources.Feeds {
				srcs = append(srcs, sources.NewFeed(f, stateDir()))
			}
		}
		names := make([]string, len(srcs))
		for i, s := range srcs {
			names[i] = fmt.Sprintf("%s every %s", s.Name(), s.Interval())
		}
		slog.Info("posting to the box", "url", b.URL, "sources", strings.Join(names, ", "))
		sources.Run(ctx, b, srcs)
		return nil

	case "send":
		title := fs.String("title", "", "a line above the text")
		color := fs.String("color", "", "#RRGGBB (default: the box picks by source)")
		align := fs.String("align", "", "center or left")
		rest, err := parse(fs, args, -1)
		if err != nil {
			return err
		}
		if len(rest) == 0 {
			return errors.New("usage: box-services send [flags] TEXT")
		}
		e := client.Text(*source, *title, strings.Join(rest, " "))
		e.Content.Color, e.Content.Align = *color, *align
		return send(e)

	case "visual":
		rest, err := parse(fs, args, 1)
		if err != nil {
			return err
		}
		if _, ok := visuals.All[rest[0]]; !ok {
			return fmt.Errorf("unknown visual %q; try %s", rest[0], strings.Join(visuals.Names(), ", "))
		}
		return send(client.Visual(*source, rest[0]))

	case "gif":
		maxSide := fs.Int("max-side", 160, "longest side sent; the box scales to the screen")
		maxFrames := fs.Int("max-frames", 120, "cap on frames")
		fps := fs.Float64("fps", 0, "frames per second (default: the GIF's own)")
		once := fs.Bool("once", false, "play once instead of looping")
		scaling := fs.String("scaling", "", "pixelated or smooth (default: by size)")
		rest, err := parse(fs, args, 1)
		if err != nil {
			return err
		}
		if *duration == 0 {
			*duration = 10
		}
		f, err := os.Open(rest[0])
		if err != nil {
			return err
		}
		defer f.Close()
		b := box()
		limit := 0
		if d, err := b.Display(); err == nil {
			limit = d.Limits.ImageBytes
		}
		r, err := gifs.Convert(f, gifs.Options{MaxSide: *maxSide, MaxFrames: *maxFrames, MaxBytes: limit, FPS: *fps})
		if err != nil {
			return fmt.Errorf("%s: %w", rest[0], err)
		}
		if *scaling == "" {
			*scaling = map[bool]string{true: "smooth", false: "pixelated"}[r.Smooth]
		}
		fmt.Printf("%s -> %s\n", filepath.Base(rest[0]), r.Describe())
		*url = b.URL
		return send(client.Image(*source, r.Width, r.Height, r.Data, r.FPS, !*once, *scaling))

	case "status":
		if _, err := parse(fs, args, 0); err != nil {
			return err
		}
		raw, err := box().Status()
		if err != nil {
			return err
		}
		return printJSON(raw)

	case "display":
		brightness := fs.Float64("brightness", -1, "0..1, until the box reboots")
		if _, err := parse(fs, args, 0); err != nil {
			return err
		}
		b := box()
		var d client.Display
		var err error
		if *brightness >= 0 || *style != "" {
			var bp *float64
			if *brightness >= 0 {
				bp = brightness
			}
			d, err = b.Settings(bp, *style)
		} else {
			d, err = b.Display()
		}
		if err != nil {
			return err
		}
		return printJSON(d)

	case "clear":
		ambient := fs.Bool("ambient", false, "also empty the idle rotation")
		if _, err := parse(fs, args, 0); err != nil {
			return err
		}
		return box().Clear(*ambient)

	case "help", "-h", "--help":
		fmt.Println(usage())
		return nil
	}
	return usage()
}

// parse handles flags before and after positional arguments, and checks
// their count (-1: any).
func parse(fs *flag.FlagSet, args []string, want int) ([]string, error) {
	var rest []string
	for {
		if err := fs.Parse(args); err != nil {
			return nil, err
		}
		if fs.NArg() == 0 {
			break
		}
		rest = append(rest, fs.Arg(0))
		args = fs.Args()[1:]
	}
	if want >= 0 && len(rest) != want {
		return nil, fmt.Errorf("%s takes %d argument(s), got %d", fs.Name(), want, len(rest))
	}
	return rest, nil
}

func printJSON(v any) error {
	out, err := json.MarshalIndent(v, "", "  ")
	if err == nil {
		fmt.Println(string(out))
	}
	return err
}

func env(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// stateDir is where feed history lives: BOX_STATE_DIR, else the XDG state dir.
func stateDir() string {
	if d := os.Getenv("BOX_STATE_DIR"); d != "" {
		return d
	}
	if d := os.Getenv("XDG_STATE_HOME"); d != "" {
		return filepath.Join(d, "box")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".local", "state", "box")
}

// loadDotenv fills in BOX_* values from the nearest .env at or above the
// working directory, without leaving the repo: a stray .env in a home
// directory must never configure the box. A real environment variable wins.
func loadDotenv() {
	dir, err := os.Getwd()
	if err != nil {
		return
	}
	for {
		if f, err := os.Open(filepath.Join(dir, ".env")); err == nil {
			defer f.Close()
			scanner := bufio.NewScanner(f)
			for scanner.Scan() {
				line := strings.TrimSpace(scanner.Text())
				key, value, ok := strings.Cut(line, "=")
				key = strings.TrimSpace(key)
				if !ok || strings.HasPrefix(line, "#") || !strings.HasPrefix(key, "BOX_") {
					continue
				}
				if _, set := os.LookupEnv(key); !set {
					os.Setenv(key, strings.Trim(strings.TrimSpace(value), `"'`))
				}
			}
			return
		}
		if _, err := os.Stat(filepath.Join(dir, ".git")); err == nil {
			return
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return
		}
		dir = parent
	}
}
