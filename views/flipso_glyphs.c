/**
 * @file flipso_glyphs.c
 * @brief The currency symbols the Flipper's fonts do not have. See flipso_glyphs.h.
 */
#include "flipso_glyphs.h"

#include <furi.h>
#include <string.h>

/* Seven rows, the height of a digit in the secondary font, so a symbol sits on
 * the same row as the digits' feet and stands as tall as the amount after it.
 * XBM order: the least significant bit is the leftmost pixel. */
#define FLIPSO_GLYPH_W 5
#define FLIPSO_GLYPH_H 7
/* The space the font leaves after each of its own glyphs. */
#define FLIPSO_GLYPH_ADVANCE (FLIPSO_GLYPH_W + 1)

/*  ..##.
 *  .#..#
 *  .#...
 *  ###..
 *  .#...
 *  .#...
 *  ##### */
static const uint8_t flipso_glyph_pound[FLIPSO_GLYPH_H] = {0x0C, 0x12, 0x02, 0x07, 0x02, 0x02, 0x1F};

/*  ..###
 *  .#...
 *  ####.
 *  .#...
 *  ####.
 *  .#...
 *  ..### */
static const uint8_t flipso_glyph_euro[FLIPSO_GLYPH_H] = {0x1C, 0x02, 0x0F, 0x02, 0x0F, 0x02, 0x1C};

typedef struct {
    const char* utf8;
    const uint8_t* bits;
} FlipsoGlyph;

static const FlipsoGlyph flipso_glyphs[] = {
    {"\xC2\xA3", flipso_glyph_pound},
    {"\xE2\x82\xAC", flipso_glyph_euro},
};

/** The hand-drawn glyph @p text starts with, or NULL. */
static const FlipsoGlyph* flipso_glyphs_at(const char* text) {
    if(((uint8_t)text[0] & 0x80) == 0) return NULL;
    for(size_t i = 0; i < COUNT_OF(flipso_glyphs); i++) {
        size_t n = strlen(flipso_glyphs[i].utf8);
        if(strncmp(text, flipso_glyphs[i].utf8, n) == 0) return &flipso_glyphs[i];
    }
    return NULL;
}

size_t flipso_glyphs_char_len(const char* text) {
    uint8_t lead = (uint8_t)text[0];
    size_t len = 1;
    if((lead & 0xE0) == 0xC0) len = 2;
    if((lead & 0xF0) == 0xE0) len = 3;
    if((lead & 0xF8) == 0xF0) len = 4;
    /* A sequence cut short by the terminator is only as long as what is there. */
    for(size_t i = 1; i < len; i++) {
        if(!flipso_glyphs_is_continuation(text[i])) return i;
    }
    return len;
}

/**
 * Walk @p text as runs of font text and hand-drawn glyphs, drawing each when
 * @p draw is set, and return the width of the whole.
 *
 * One walk for both jobs, so the width a line is wrapped to and the width it is
 * drawn at cannot disagree.
 */
static uint16_t flipso_glyphs_walk(Canvas* canvas, int32_t x, int32_t y, const char* text, bool draw) {
    /* Runs are copied out to be terminated. A run longer than this is split,
     * which costs nothing: the font draws the halves exactly as it would the
     * whole. */
    char run[48];
    size_t fill = 0;
    uint16_t width = 0;

    for(const char* p = text;; ) {
        const FlipsoGlyph* glyph = *p ? flipso_glyphs_at(p) : NULL;
        bool end = *p == '\0';

        if(end || glyph || fill + 1 >= sizeof(run)) {
            if(fill) {
                run[fill] = '\0';
                if(draw) canvas_draw_str(canvas, x + width, y, run);
                width = (uint16_t)(width + canvas_string_width(canvas, run));
                fill = 0;
            }
        }
        if(end) break;

        if(glyph) {
            if(draw) {
                /* The font's glyphs stand on the row above the y it is given,
                 * so the symbol's foot goes there too. */
                canvas_draw_xbm(
                    canvas, x + width, y - FLIPSO_GLYPH_H, FLIPSO_GLYPH_W, FLIPSO_GLYPH_H,
                    glyph->bits);
            }
            width = (uint16_t)(width + FLIPSO_GLYPH_ADVANCE);
            p += strlen(glyph->utf8);
            continue;
        }

        run[fill++] = *p++;
    }

    return width;
}

uint16_t flipso_glyphs_width(Canvas* canvas, const char* text) {
    return flipso_glyphs_walk(canvas, 0, 0, text, false);
}

void flipso_glyphs_draw(Canvas* canvas, int32_t x, int32_t y, const char* text) {
    flipso_glyphs_walk(canvas, x, y, text, true);
}
