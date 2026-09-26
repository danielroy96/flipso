/**
 * @file flipso_glyphs.h
 * @brief Draw and measure text that may contain the currency symbols.
 *
 * The Flipper's fonts are the ASCII-only u8g2 "_tr" sets, so "£" and "€" have
 * no glyph: u8g2 skips the bytes, and a balance would read "24.15" with nothing
 * in front of it. These two are drawn by hand instead, from small bitmaps sized
 * to the secondary font's digits, and everything else goes to the font as
 * usual. Anything that draws a string which might hold an amount of money goes
 * through here rather than straight to canvas_draw_str.
 *
 * The symbols arrive as UTF-8, which is what the formatting code writes and
 * what a C string literal holds, so the rest of the app never has to know the
 * font lacks them.
 */
#pragma once

#include <gui/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Width in pixels of @p text in the canvas's current font. */
uint16_t flipso_glyphs_width(Canvas* canvas, const char* text);

/** Draw @p text with its baseline at @p y, as canvas_draw_str does. */
void flipso_glyphs_draw(Canvas* canvas, int32_t x, int32_t y, const char* text);

/**
 * Length of the UTF-8 sequence starting at @p text, 1 for anything that is not
 * the lead byte of one. Used to trim a string without cutting a symbol in half.
 */
size_t flipso_glyphs_char_len(const char* text);

/** True when @p byte continues a UTF-8 sequence rather than starting one. */
static inline bool flipso_glyphs_is_continuation(char byte) {
    return ((uint8_t)byte & 0xC0) == 0x80;
}

#ifdef __cplusplus
}
#endif
