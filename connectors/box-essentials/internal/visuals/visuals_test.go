package visuals

import (
	"bytes"
	"testing"
)

const w, h = 16, 16

func split(data []byte) [][]byte {
	var out [][]byte
	for i := 0; i < len(data); i += w * h * 3 {
		out = append(out, data[i:i+w*h*3])
	}
	return out
}

func TestEveryVisualRendersWholeFrames(t *testing.T) {
	for name, v := range All {
		data := v.Render(w, h)
		if len(data) == 0 || len(data)%(w*h*3) != 0 {
			t.Fatalf("%s: %d bytes", name, len(data))
		}
		if !bytes.Equal(data, v.Render(w, h)) {
			t.Fatalf("%s is not deterministic", name)
		}
	}
	if got := len(Rainbow(32, 8, 3, 12, 1)); got != 3*32*8*3 {
		t.Fatalf("non-square rainbow: %d", got)
	}
}

func TestHSV(t *testing.T) {
	if HSV(0, 1, 1) != (rgb{255, 0, 0}) || HSV(1.0/3, 1, 1) != (rgb{0, 255, 0}) || HSV(1.25, 1, 1) != HSV(0.25, 1, 1) {
		t.Fatal("hsv")
	}
}

func TestRainbowAnimates(t *testing.T) {
	frames := split(Rainbow(w, h, 6, 12, 5))
	for i := 1; i < len(frames); i++ {
		if bytes.Equal(frames[i], frames[i-1]) {
			t.Fatal("frames repeat")
		}
	}
}

func TestLife(t *testing.T) {
	blinker := make([]bool, 25)
	blinker[11], blinker[12], blinker[13] = true, true, true
	next := Step(blinker, 5, 5)
	if !next[7] || !next[12] || !next[17] || next[11] || next[13] {
		t.Fatal("a blinker must turn vertical")
	}
	for i, f := range split(Life(w, h, 400, 2)) {
		if bytes.Count(f, []byte{0}) == len(f) {
			t.Fatalf("frame %d is blank: life must reseed", i)
		}
	}
}

func TestBeerPoursThenRests(t *testing.T) {
	frames := split(Beer(w, h, 20))
	lit := func(f []byte) int {
		n := 0
		for _, b := range f {
			if b != 0 {
				n++
			}
		}
		return n
	}
	if lit(frames[len(frames)-1]) <= lit(frames[len(frames)/4]) {
		t.Fatal("the glass should fill up")
	}
	if !bytes.Equal(frames[len(frames)-1], frames[len(frames)-2]) {
		t.Fatal("it should end at rest")
	}
}
