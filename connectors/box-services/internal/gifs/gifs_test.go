package gifs

import (
	"bytes"
	"image"
	"image/color"
	"image/color/palette"
	"image/gif"
	"testing"
)

// animation encodes n frames of size w x h, frame i a flat colour.
func animation(w, h, n, delay int) []byte {
	g := &gif.GIF{}
	for i := range n {
		img := image.NewPaletted(image.Rect(0, 0, w, h), palette.Plan9)
		c := color.RGBA{uint8(40 * (i + 1)), 0, 0, 255}
		for y := range h {
			for x := range w {
				img.Set(x, y, c)
			}
		}
		g.Image = append(g.Image, img)
		g.Delay = append(g.Delay, delay)
	}
	var buf bytes.Buffer
	gif.EncodeAll(&buf, g)
	return buf.Bytes()
}

func TestSmallGIFsKeepTheirSize(t *testing.T) {
	r, err := Convert(bytes.NewReader(animation(32, 16, 3, 10)), Options{})
	if err != nil || r.Width != 32 || r.Height != 16 || r.Frames != 3 || r.FPS != 10 || r.Smooth {
		t.Fatalf("%+v %v", r, err)
	}
	if len(r.Data) != 3*32*16*3 || r.Data[0] == r.Data[32*16*3] {
		t.Fatal("frames should differ")
	}
}

func TestLargeGIFsShrinkToFit(t *testing.T) {
	r, err := Convert(bytes.NewReader(animation(400, 200, 2, 5)), Options{MaxSide: 160})
	if err != nil || r.Width != 160 || r.Height != 80 || !r.Smooth {
		t.Fatalf("%+v %v", r, err)
	}
	r, err = Convert(bytes.NewReader(animation(100, 100, 10, 5)), Options{MaxBytes: 10 * 50 * 50 * 3})
	if err != nil || len(r.Data) > 10*50*50*3 {
		t.Fatalf("over the byte limit: %d", len(r.Data))
	}
}

func TestFrameLimitAndBadInput(t *testing.T) {
	r, _ := Convert(bytes.NewReader(animation(8, 8, 10, 10)), Options{MaxFrames: 4})
	if r.Frames != 4 {
		t.Fatalf("%d frames", r.Frames)
	}
	if _, err := Convert(bytes.NewReader([]byte("not a gif")), Options{}); err == nil {
		t.Fatal("want an error")
	}
}
