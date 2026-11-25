"""Built-in visuals: named animations, rendered here and sent as frames.

A feed or `box-sources visual beer` asks for one by name; the client renders
it at the panel size the box reports and uploads the pixels (see client.py).
The box itself stays small: it plays frames and knows nothing about beer.

Rendering depends only on the name and the panel size, so it is done once per
size and cached.
"""

from collections.abc import Callable
from dataclasses import dataclass
from functools import lru_cache

from .animations import beer_frames, gradient_frames, life_frames, rainbow_frames


@dataclass(frozen=True)
class Visual:
    #: (width, height) -> count * width * height * 3 bytes of RGB frames.
    render: Callable[[int, int], bytes]
    fps: float
    #: False plays once and rests on the last frame.
    loop: bool = True


#: Seeded so a visual looks the same every time it is asked for; the
#: generators take `seed=None` for variety when called directly.
VISUALS: dict[str, Visual] = {
    "rainbow": Visual(lambda w, h: rainbow_frames(w, h, 180, 12.0, seed=1), 12.0),
    "life": Visual(lambda w, h: life_frames(w, h, 240, seed=1), 8.0),
    "gradient": Visual(lambda w, h: gradient_frames(w, h, 120), 12.0),
    "beer": Visual(lambda w, h: beer_frames(w, h, 20.0), 20.0, loop=False),
}


@lru_cache(maxsize=16)
def frames(name: str, width: int, height: int) -> bytes:
    """The rendered frames for `name` at this size. Slow the first time."""
    return VISUALS[name].render(width, height)


__all__ = ["VISUALS", "Visual", "frames"]
