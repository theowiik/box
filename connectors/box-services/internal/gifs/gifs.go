// Package gifs turns a GIF into image content for the box.
//
// Decoding happens here, on whatever machine sends it, so the box only ever
// receives raw RGB frames and never needs an image decoder.
package gifs

import (
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/gif"
	"io"
)

// Options shape the conversion.
type Options struct {
	MaxSide   int     // longest side sent; the box scales to the screen (0: 160)
	MaxFrames int     // cap on frames (0: 120)
	MaxBytes  int     // the box's upload limit (0: 4 MiB)
	FPS       float64 // 0: from the GIF's own frame delays
}

// Result is the converted animation.
type Result struct {
	Width, Height int
	FPS           float64
	Frames        int
	Data          []byte // Frames * Width * Height * 3 RGB bytes
	Smooth        bool   // large enough that smooth scaling suits it better than blocks
}

// Convert decodes a GIF, composites its frames (honouring disposal), and
// scales them down if needed to fit MaxSide and MaxBytes. Transparency
// becomes black.
func Convert(r io.Reader, o Options) (Result, error) {
	if o.MaxSide == 0 {
		o.MaxSide = 160
	}
	if o.MaxFrames == 0 {
		o.MaxFrames = 120
	}
	if o.MaxBytes == 0 {
		o.MaxBytes = 4 << 20
	}
	g, err := gif.DecodeAll(r)
	if err != nil {
		return Result{}, err
	}
	if len(g.Image) == 0 {
		return Result{}, errors.New("the GIF has no frames")
	}
	bounds := image.Rect(0, 0, g.Config.Width, g.Config.Height)
	if bounds.Empty() {
		bounds = g.Image[0].Bounds()
	}
	frames := composite(g, bounds, o.MaxFrames)

	w, h := bounds.Dx(), bounds.Dy()
	scale := min(1.0, float64(o.MaxSide)/float64(max(w, h)))
	for {
		tw, th := max(1, int(float64(w)*scale)), max(1, int(float64(h)*scale))
		if len(frames)*tw*th*3 <= o.MaxBytes || (tw == 1 && th == 1) {
			w, h = tw, th
			break
		}
		scale *= 0.9
	}

	data := make([]byte, 0, len(frames)*w*h*3)
	for _, f := range frames {
		data = appendScaled(data, f, w, h)
	}
	fps := o.FPS
	if fps == 0 {
		total := 0
		for _, d := range g.Delay[:len(frames)] {
			total += max(d, 2) // browsers treat 0-1 as 10 ms; 20 ms is the honest floor
		}
		fps = min(60, max(0.1, float64(len(frames))*100/float64(total)))
	}
	return Result{Width: w, Height: h, FPS: fps, Frames: len(frames), Data: data,
		Smooth: max(w, h) > 96}, nil
}

func composite(g *gif.GIF, bounds image.Rectangle, limit int) []*image.RGBA {
	canvas := image.NewRGBA(bounds)
	draw.Draw(canvas, bounds, image.NewUniform(color.Black), image.Point{}, draw.Src)
	var frames []*image.RGBA
	for i, img := range g.Image {
		if len(frames) == limit {
			break
		}
		var previous *image.RGBA
		if g.Disposal != nil && g.Disposal[i] == gif.DisposalPrevious {
			previous = image.NewRGBA(bounds)
			copy(previous.Pix, canvas.Pix)
		}
		draw.Draw(canvas, img.Bounds(), img, img.Bounds().Min, draw.Over)
		frame := image.NewRGBA(bounds)
		copy(frame.Pix, canvas.Pix)
		frames = append(frames, frame)
		switch {
		case g.Disposal == nil:
		case g.Disposal[i] == gif.DisposalBackground:
			draw.Draw(canvas, img.Bounds(), image.NewUniform(color.Black), image.Point{}, draw.Src)
		case previous != nil:
			canvas = previous
		}
	}
	return frames
}

// appendScaled samples src down (nearest neighbour) to w x h RGB.
func appendScaled(out []byte, src *image.RGBA, w, h int) []byte {
	b := src.Bounds()
	for y := range h {
		sy := b.Min.Y + y*b.Dy()/h
		for x := range w {
			sx := b.Min.X + x*b.Dx()/w
			i := src.PixOffset(sx, sy)
			a := uint32(src.Pix[i+3])
			// Composite onto black.
			out = append(out,
				uint8(uint32(src.Pix[i])*a/255),
				uint8(uint32(src.Pix[i+1])*a/255),
				uint8(uint32(src.Pix[i+2])*a/255))
		}
	}
	return out
}

// Describe is a one-line summary, e.g. "40 frames at 120x90, 12.5 fps (1.3 MB)".
func (r Result) Describe() string {
	return fmt.Sprintf("%d frames at %dx%d, %.1f fps (%.1f MB)", r.Frames, r.Width, r.Height,
		r.FPS, float64(len(r.Data))/1e6)
}
