// Package visuals renders the built-in animations as RGB frames.
//
// A source asks for one by name; the client renders it at a size the box
// accepts and sends it as image content. The box knows nothing about beer.
package visuals

import (
	"math"
	"math/rand/v2"
	"slices"
	"sort"
	"sync"
)

// Visual renders an animation: Render(w, h) is a whole number of w*h*3 RGB frames.
type Visual struct {
	Render func(w, h int) []byte
	FPS    float64
	Loop   bool // false plays once and rests on the last frame
}

// All are the built-in visuals, seeded so each looks the same every time.
var All = map[string]Visual{
	"rainbow":  {Render: cached("rainbow", func(w, h int) []byte { return Rainbow(w, h, 180, 12, 1) }), FPS: 12, Loop: true},
	"life":     {Render: cached("life", func(w, h int) []byte { return Life(w, h, 240, 1) }), FPS: 8, Loop: true},
	"gradient": {Render: cached("gradient", func(w, h int) []byte { return Gradient(w, h, 120) }), FPS: 12, Loop: true},
	"beer":     {Render: cached("beer", func(w, h int) []byte { return Beer(w, h, 20) }), FPS: 20, Loop: false},
}

// Names lists the visuals, sorted.
func Names() []string {
	names := make([]string, 0, len(All))
	for n := range All {
		names = append(names, n)
	}
	sort.Strings(names)
	return names
}

var cache sync.Map

// cached memoizes a renderer per size: output depends only on name and size.
func cached(name string, render func(w, h int) []byte) func(w, h int) []byte {
	return func(w, h int) []byte {
		key := [3]any{name, w, h}
		if v, ok := cache.Load(key); ok {
			return v.([]byte)
		}
		out := render(w, h)
		cache.Store(key, out)
		return out
	}
}

type rgb [3]uint8

// HSV converts hue (wrapping), saturation and value (0..1) to RGB.
func HSV(h, s, v float64) rgb {
	h -= math.Floor(h)
	s, v = clamp(s, 0, 1), clamp(v, 0, 1)
	i := int(h * 6)
	f := h*6 - float64(i)
	p, q, t := v*(1-s), v*(1-s*f), v*(1-s*(1-f))
	var r, g, b float64
	switch i % 6 {
	case 0:
		r, g, b = v, t, p
	case 1:
		r, g, b = q, v, p
	case 2:
		r, g, b = p, v, t
	case 3:
		r, g, b = p, q, v
	case 4:
		r, g, b = t, p, v
	default:
		r, g, b = v, p, q
	}
	return rgb{uint8(r*255 + 0.5), uint8(g*255 + 0.5), uint8(b*255 + 0.5)}
}

func clamp(x, lo, hi float64) float64 { return min(max(x, lo), hi) }

type canvas struct {
	w, h int
	buf  []byte
}

func newCanvas(w, h int) *canvas { return &canvas{w, h, make([]byte, w*h*3)} }

func (c *canvas) set(x, y int, col rgb) {
	if x >= 0 && y >= 0 && x < c.w && y < c.h {
		copy(c.buf[(y*c.w+x)*3:], col[:])
	}
}

func (c *canvas) get(x, y int) rgb {
	i := (y*c.w + x) * 3
	return rgb{c.buf[i], c.buf[i+1], c.buf[i+2]}
}

// --- value noise --------------------------------------------------------------

// Noise is animated value noise, sampled as n.At(x, y, t) in 0..1.
type Noise struct{ seed uint32 }

func (n Noise) hash(x, y, z int) float64 {
	v := uint32(x)*374761393 + uint32(y)*668265263 + uint32(z)*2147483647 + n.seed
	v = (v ^ (v >> 13)) * 1274126177
	return float64(v^(v>>16)) / float64(math.MaxUint32)
}

func (n Noise) At(x, y, z float64) float64 {
	xi, yi, zi := math.Floor(x), math.Floor(y), math.Floor(z)
	xf, yf, zf := x-xi, y-yi, z-zi
	u, v, w := xf*xf*(3-2*xf), yf*yf*(3-2*yf), zf*zf*(3-2*zf)
	plane := func(dz int) float64 {
		z0 := int(zi) + dz
		n00, n10 := n.hash(int(xi), int(yi), z0), n.hash(int(xi)+1, int(yi), z0)
		n01, n11 := n.hash(int(xi), int(yi)+1, z0), n.hash(int(xi)+1, int(yi)+1, z0)
		top, bottom := n00+(n10-n00)*u, n01+(n11-n01)*u
		return top + (bottom-top)*v
	}
	near := plane(0)
	return near + (plane(1)-near)*w
}

func (n Noise) FBM(x, y, z float64, octaves int) float64 {
	total, amp, freq, norm := 0.0, 1.0, 1.0, 0.0
	for range octaves {
		total += n.At(x*freq, y*freq, z*freq) * amp
		norm += amp
		amp *= 0.5
		freq *= 2
	}
	return total / norm
}

// --- rainbow ------------------------------------------------------------------

// Rainbow is a hue gradient with drifting blobs and a noise wash.
func Rainbow(w, h, count int, fps float64, seed uint32) []byte {
	noise, rng := Noise{seed}, Noise{seed}
	type blob struct{ fx, fy, px, py, r float64 }
	var blobs []blob
	for i := range 4 {
		blobs = append(blobs, blob{
			0.11 + 0.07*rng.At(float64(i), 0, 0), 0.09 + 0.06*rng.At(float64(i), 1, 0),
			6.283 * rng.At(float64(i), 2, 0), 6.283 * rng.At(float64(i), 3, 0),
			0.18 + 0.22*rng.At(float64(i), 4, 0),
		})
	}
	short := float64(min(w, h))
	out := make([]byte, 0, count*w*h*3)
	for index := range count {
		t := float64(index) / fps
		type centre struct{ x, y, r2 float64 }
		centres := make([]centre, len(blobs))
		for i, b := range blobs {
			centres[i] = centre{
				(0.5 + 0.42*math.Sin(t*b.fx*6.283+b.px)) * float64(w),
				(0.5 + 0.42*math.Cos(t*b.fy*6.283+b.py)) * float64(h),
				(b.r * short) * (b.r * short),
			}
		}
		for y := range h {
			rowHue := float64(y)/float64(max(h-1, 1))*0.12 + t*0.08
			for x := range w {
				field := 0.0
				for _, c := range centres {
					dx, dy := float64(x)-c.x, float64(y)-c.y
					field += c.r2 / (dx*dx + dy*dy + c.r2*0.35)
				}
				n := noise.FBM(float64(x)*0.22, float64(y)*0.35, t*0.35, 2)
				hue := rowHue + float64(x)/float64(w)*0.85 + field*0.10 + (n-0.5)*0.125
				value := (0.35 + 0.5*min(field, 1.4)/1.4) * (1 - 0.15*(1-n))
				col := HSV(hue, 1, value)
				out = append(out, col[:]...)
			}
		}
	}
	return out
}

// --- life ---------------------------------------------------------------------

// maxAge is how many generations a cell's colour keeps shifting.
const maxAge = 24

// Step is one generation of Conway's Life on a wrapping grid.
func Step(cells []bool, w, h int) []bool {
	next := make([]bool, len(cells))
	for y := range h {
		for x := range w {
			n := 0
			for dy := -1; dy <= 1; dy++ {
				for dx := -1; dx <= 1; dx++ {
					if (dx != 0 || dy != 0) && cells[((y+dy+h)%h)*w+(x+dx+w)%w] {
						n++
					}
				}
			}
			alive := cells[y*w+x]
			next[y*w+x] = n == 3 || (alive && n == 2)
		}
	}
	return next
}

// Life is the Game of Life, one frame per generation, reseeded whenever the
// board dies out or starts repeating, so it never freezes.
func Life(w, h, count int, seed uint64) []byte {
	rng := rand.New(rand.NewPCG(seed, seed))
	random := func() []bool {
		c := make([]bool, w*h)
		for i := range c {
			c[i] = rng.Float64() < 0.35
		}
		return c
	}
	colors := make([]rgb, maxAge+1)
	for a := range colors {
		colors[a] = HSV(0.45+0.35*float64(a)/maxAge, 0.85, 1)
	}
	cells, ages := random(), make([]int, w*h)
	var history []string
	out := make([]byte, 0, count*w*h*3)
	for range count {
		for i, alive := range cells {
			if alive {
				out = append(out, colors[ages[i]][:]...)
			} else {
				out = append(out, 0, 0, 0)
			}
		}
		cells = Step(cells, w, h)
		alive := 0
		for i, a := range cells {
			if a {
				ages[i] = min(ages[i]+1, maxAge)
				alive++
			} else {
				ages[i] = 0
			}
		}
		state := key(cells)
		if alive == 0 || slices.Contains(history, state) {
			cells, history = random(), nil
			clear(ages)
		} else if history = append(history, state); len(history) > 24 {
			history = history[1:]
		}
	}
	return out
}

func key(cells []bool) string {
	b := make([]byte, (len(cells)+7)/8)
	for i, c := range cells {
		if c {
			b[i/8] |= 1 << (i % 8)
		}
	}
	return string(b)
}

// --- gradient -------------------------------------------------------------------

// Gradient is a plain hue sweep.
func Gradient(w, h, count int) []byte {
	out := make([]byte, 0, count*w*h*3)
	for index := range count {
		phase := float64(index) / float64(count)
		for range h {
			for x := range w {
				col := HSV(float64(x)/float64(w)+phase, 1, 1)
				out = append(out, col[:]...)
			}
		}
	}
	return out
}

// --- beer -------------------------------------------------------------------------

var (
	beerTop    = rgb{255, 186, 32}
	beerBottom = rgb{176, 92, 0}
	foamCol    = rgb{255, 244, 218}
	foamShade  = rgb{226, 212, 180}
	bubbleCol  = rgb{255, 222, 130}
	glassCol   = rgb{64, 80, 96}
	glassShine = rgb{120, 140, 160}
)

func mix(a, b rgb, t float64) rgb {
	return rgb{
		uint8(float64(a[0]) + (float64(b[0])-float64(a[0]))*t),
		uint8(float64(a[1]) + (float64(b[1])-float64(a[1]))*t),
		uint8(float64(a[2]) + (float64(b[2])-float64(a[2]))*t),
	}
}

// Beer pours a pint: the stream, rising beer and foam, then bubbles. It plays
// once and rests on a full glass. Geometry scales from a 32x32 design.
func Beer(w, h int, fps float64) []byte {
	s := float64(min(w, h)) / 32
	cx := float64(w) / 2
	top := int(math.Round(float64(h)/2 - 12*s))
	bottom := int(math.Round(float64(h)/2 + 12*s))
	topHW, bottomHW := 8.5*s, 6.0*s
	base := max(1, int(math.Round(1.5*s)))
	foamMax := float64(max(2, int(math.Round(3*s))))

	type wall struct{ left, right int }
	walls := make([]wall, bottom-top+1) // indexed by y - top
	for y := top; y <= bottom; y++ {
		frac := float64(y-top) / float64(max(bottom-top, 1))
		hw := topHW + (bottomHW-topHW)*frac
		walls[y-top] = wall{int(math.Round(cx - hw)), int(math.Round(cx+hw)) - 1}
	}
	floor := bottom - base
	full := top - 1

	const tDrop, tFill, tStop, tRest = 0.35, 3.2, 0.3, 1.6
	count := int(math.Round((tDrop + tFill + tStop + tRest) * fps))
	rng := rand.New(rand.NewPCG(7, 7))
	type bubble struct{ x, y, speed float64 }
	bubbles := make([]bubble, max(4, int(math.Round(9*s*s))))
	for i := range bubbles {
		bubbles[i] = bubble{rng.Float64(), rng.Float64(), (4 + 5*rng.Float64()) * s}
	}

	out := make([]byte, 0, count*w*h*3)
	for index := range count {
		t := float64(index) / fps
		c := newCanvas(w, h)
		p := clamp((t-tDrop)/tFill, 0, 1)
		level := 1 - (1-p)*(1-p)
		surface := float64(floor+1) - float64(floor+1-full)*level
		foam := 0.0
		if level > 0 {
			foam = foamMax * min(1, level*1.6)
		}

		for y := top - 1; y <= floor; y++ {
			cover := min(1, float64(y+1)-surface)
			if cover <= 0 {
				continue
			}
			l, r := walls[0].left+1, walls[0].right-1 // the dome above the rim
			if y >= top {
				l, r = walls[y-top].left, walls[y-top].right
			}
			head := float64(y) < surface+foam
			for x := l + 1; x < r; x++ {
				var col rgb
				switch {
				case head && (x*7+y*3)%5 == 0:
					col = foamShade
				case head:
					col = foamCol
				default:
					col = mix(beerTop, beerBottom, float64(y-top)/float64(max(floor-top, 1)))
				}
				if cover < 1 {
					col = mix(rgb{}, col, cover)
				}
				c.set(x, y, col)
			}
		}

		if level > 0.15 {
			span := max(float64(floor)-surface-foam, 1)
			// Bubbles settle once the pour stops, so the pint ends at rest.
			bt := min(t, tDrop+tFill+tStop)
			for _, b := range bubbles {
				y := float64(floor) - math.Mod(b.y*(float64(floor)-surface)+bt*b.speed, span)
				yi := int(y)
				l, r := 0, 0
				if yi >= top && yi <= bottom {
					l, r = walls[yi-top].left, walls[yi-top].right
				}
				x := l + 1 + int(b.x*float64(max(r-l-1, 1)))
				if surface+foam+1 <= y && y <= float64(floor) && l < x && x < r {
					c.set(x, yi, bubbleCol)
				}
			}
		}

		tEnd := tDrop + tFill
		if t < tEnd+tStop {
			headY := min(1, t/tDrop) * surface
			tailY := max(0, (t-tEnd)/tStop) * surface
			sx := int(cx)
			if s < 1.5 {
				sx--
			}
			for y := int(tailY); y < int(headY); y++ {
				c.set(sx, y, beerTop)
				if s >= 1.5 {
					c.set(sx-1, y, beerTop)
				}
			}
		}

		for y := top; y <= bottom; y++ {
			wl := walls[y-top]
			c.set(wl.left, y, glassCol)
			c.set(wl.right, y, glassCol)
			if y > floor {
				for x := wl.left; x <= wl.right; x++ {
					c.set(x, y, glassCol)
				}
			}
		}
		for y := top + 2; y < top+2+int(math.Round(8*s)) && y <= bottom; y++ {
			x := walls[y-top].left + 1
			if x >= 0 && x < w && y >= 0 && y < h {
				c.set(x, y, mix(c.get(x, y), glassShine, 0.45))
			}
		}
		out = append(out, c.buf...)
	}
	return out
}
