/* Host stub: a 128x64 mono framebuffer that records what a view draws. */
#pragma once
#include <furi.h>

#define STUB_W               128
#define STUB_H               64
/* Approximates the Flipper's proportional fonts closely enough to exercise the
 * truncation loop; exact metrics are the device's business, not the logic's. */
#define STUB_GLYPH_W         5
#define STUB_GLYPH_PRIMARY_W 6

typedef enum {
    ColorWhite,
    ColorBlack
} Color;
typedef enum {
    FontPrimary,
    FontSecondary
} Font;
typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter
} Align;

typedef struct Icon {
    uint8_t width;
    uint8_t height;
    char mark; /* Drawn into the framebuffer so icons are identifiable. */
} Icon;

#define STUB_MAX_TEXTS    32
#define STUB_MAX_TEXT_LEN 64

typedef struct {
    char pixels[STUB_H][STUB_W];
    Color color;
    Font font;
    /* Every string drawn this frame, kept verbatim: reading them back out of
     * the framebuffer would lose the spaces inside a label. */
    char texts[STUB_MAX_TEXTS][STUB_MAX_TEXT_LEN];
    int text_x[STUB_MAX_TEXTS]; /* Where each one started, for indents. */
    int text_count;
} Canvas;

static inline uint16_t icon_get_width(const Icon* icon) {
    return icon->width;
}
static inline uint16_t icon_get_height(const Icon* icon) {
    return icon->height;
}

static inline void canvas_clear(Canvas* c) {
    memset(c->pixels, '.', sizeof(c->pixels));
    c->text_count = 0;
}
static inline void canvas_set_color(Canvas* c, Color color) {
    c->color = color;
}
static inline void canvas_set_font(Canvas* c, Font font) {
    c->font = font;
}

static inline uint16_t canvas_string_width(Canvas* c, const char* s) {
    size_t w = (c->font == FontPrimary) ? STUB_GLYPH_PRIMARY_W : STUB_GLYPH_W;
    return (uint16_t)(strlen(s) * w);
}

static inline void stub_put(Canvas* c, int x, int y, char ch) {
    if(x < 0 || x >= STUB_W || y < 0 || y >= STUB_H) return;
    c->pixels[y][x] = ch;
}

static inline void canvas_draw_line(Canvas* c, int x0, int y0, int x1, int y1) {
    if(y0 == y1) {
        for(int x = x0; x <= x1; x++)
            stub_put(c, x, y0, '-');
    } else {
        for(int y = y0; y <= y1; y++)
            stub_put(c, x0, y, '|');
    }
}

static inline void canvas_draw_box(Canvas* c, int x, int y, int w, int h) {
    for(int j = 0; j < h; j++)
        for(int i = 0; i < w; i++)
            stub_put(c, x + i, y + j, c->color == ColorBlack ? '#' : ' ');
}

static inline void canvas_draw_icon(Canvas* c, int x, int y, const Icon* icon) {
    furi_assert(icon);
    for(int j = 0; j < icon->height; j++)
        for(int i = 0; i < icon->width; i++)
            stub_put(c, x + i, y + j, icon->mark);
}

/* Bitmaps are drawn pixel for pixel, LSB leftmost as XBM is, in their own mark
 * so a test can tell a hand-drawn glyph from text. */
static inline void
    canvas_draw_xbm(Canvas* c, int x, int y, size_t w, size_t h, const uint8_t* bits) {
    size_t stride = (w + 7) / 8;
    for(size_t j = 0; j < h; j++)
        for(size_t i = 0; i < w; i++)
            if(bits[j * stride + i / 8] & (1 << (i % 8))) stub_put(c, x + (int)i, y + (int)j, '%');
}

/* Text lands one character per glyph cell so the render stays readable, and is
 * recorded verbatim so tests can match on it. */
static inline void stub_text(Canvas* c, int x, int y, const char* s) {
    size_t w = (c->font == FontPrimary) ? STUB_GLYPH_PRIMARY_W : STUB_GLYPH_W;
    for(size_t i = 0; s[i]; i++)
        stub_put(c, x + (int)(i * w), y, s[i]);
    if(c->text_count < STUB_MAX_TEXTS) {
        c->text_x[c->text_count] = x;
        snprintf(c->texts[c->text_count++], STUB_MAX_TEXT_LEN, "%s", s);
    }
}

/* The device draws from the text baseline; the harness works in top-left rows,
 * so shift up by the font height to land in the same row the device would. */
static inline void canvas_draw_str(Canvas* c, int x, int y, const char* s) {
    stub_text(c, x, y - 5, s);
}

static inline void
    canvas_draw_str_aligned(Canvas* c, int x, int y, Align h, Align v, const char* s) {
    UNUSED(v);
    size_t w = (c->font == FontPrimary) ? STUB_GLYPH_PRIMARY_W : STUB_GLYPH_W;
    int start = x;
    if(h == AlignCenter) start = x - (int)(strlen(s) * w) / 2;
    if(h == AlignRight) start = x - (int)(strlen(s) * w);
    stub_text(c, start, y, s);
}
