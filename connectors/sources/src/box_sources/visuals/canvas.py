"""A tiny RGB canvas the generators draw into, one frame at a time."""

RGB = tuple[int, int, int]


class Canvas:
    """A width x height RGB buffer, row-major from the top-left."""

    __slots__ = ("width", "height", "buf")

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.buf = bytearray(width * height * 3)

    def set_px(self, x: int, y: int, color: RGB) -> None:
        """Out-of-bounds coordinates are ignored."""
        if 0 <= x < self.width and 0 <= y < self.height:
            i = (y * self.width + x) * 3
            self.buf[i : i + 3] = bytes(color)
