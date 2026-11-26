#include "box/led.h"

#include <string.h>

void box_led_render(const box_frame_t *grid, box_frame_t *screen)
{
    const int sx = screen->width / grid->width, sy = screen->height / grid->height;
    const int pitch = sx < sy ? sx : sy;
    if (pitch < 1) {
        return;
    }
    const int x0 = (screen->width - pitch * grid->width) / 2;
    const int y0 = (screen->height - pitch * grid->height) / 2;
    memset(screen->pixels, 0, box_frame_bytes(screen));
    for (int gy = 0; gy < grid->height; ++gy) {
        uint8_t *row = screen->pixels + ((size_t)(y0 + gy * pitch) * screen->width + x0) * 3;
        const uint8_t *src = grid->pixels + (size_t)gy * grid->width * 3;
        for (int gx = 0; gx < grid->width; ++gx, src += 3) {
            for (int k = 0; k < pitch; ++k) {
                memcpy(row + ((size_t)gx * pitch + k) * 3, src, 3);
            }
        }
        /* The block's other rows repeat the first. */
        const size_t bytes = (size_t)grid->width * pitch * 3;
        for (int k = 1; k < pitch; ++k) {
            memcpy(row + (size_t)k * screen->width * 3, row, bytes);
        }
    }
}
