/* Host stub: the two element helpers the icon list draws with. */
#pragma once
#include <gui/canvas.h>

static inline void elements_slightly_rounded_box(Canvas* c, int x, int y, size_t w, size_t h) {
    canvas_draw_box(c, x, y, (int)w, (int)h);
}

static inline void
    elements_scrollbar_pos(Canvas* c, int x, int y, size_t h, size_t pos, size_t total) {
    furi_assert(total > 0);
    furi_assert(pos < total);
    for(size_t i = 0; i < h; i++) stub_put(c, x - 1, y + (int)i, ':');
    /* Mark roughly where the handle would sit, so the render shows it moving. */
    size_t handle = h * pos / total;
    stub_put(c, x - 1, y + (int)handle, 'H');
}
