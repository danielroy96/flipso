/**
 * @file flipso_text_view.c
 * @brief Word wrapping, drawing and input for the scrolling text panel.
 *
 * Wrapping happens in the draw callback because it is the only place a Canvas
 * exists, and the glyph widths it measures are what decide where a line breaks.
 * The whole text is walked on every frame: it is a few hundred bytes, and
 * caching it would mean invalidating the cache on a font change that the view
 * never sees.
 */
#include "flipso_text_view.h"

#include <furi.h>
#include <gui/elements.h>

#define FLIPSO_TEXT_SCREEN_W 128
#define FLIPSO_TEXT_SCREEN_H 64
/* Baseline pitch. FontSecondary is 8px tall; 11 leaves it legible without
 * wasting a row of the six that fit. */
#define FLIPSO_TEXT_LINE_H 11
#define FLIPSO_TEXT_X      2
/* Text stops short of the right edge so a scrollbar never overlaps a glyph. */
#define FLIPSO_TEXT_RIGHT (FLIPSO_TEXT_SCREEN_W - 5)
/* Longest wrapped line we will assemble. No line of a 128px screen comes near
 * this, and a source line longer than it is hard-broken like any other. */
#define FLIPSO_TEXT_LINE_MAX 96

typedef struct {
    FuriString* text;
    uint16_t scroll; /**< First wrapped line drawn, in lines from the top. */
    uint16_t lines; /**< Total wrapped lines, counted by the last draw. */
} FlipsoTextModel;

struct FlipsoTextView {
    View* view;
};

/** Rows of text that fit on screen. */
static uint16_t flipso_text_rows(void) {
    return FLIPSO_TEXT_SCREEN_H / FLIPSO_TEXT_LINE_H;
}

/**
 * Emit one wrapped line.
 *
 * Called for every line of the text in order, whether or not it is on screen,
 * because the total is what the scrollbar needs. @p canvas is NULL when the
 * line falls outside the visible window, which is how counting and drawing
 * share one pass.
 */
static void flipso_text_emit(Canvas* canvas, const char* line, uint16_t row, bool bold) {
    if(!canvas) return;
    canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);
    canvas_draw_str(
        canvas, FLIPSO_TEXT_X, (int)(row * FLIPSO_TEXT_LINE_H + FLIPSO_TEXT_LINE_H - 2), line);
}

/**
 * Break @p src into lines that fit @p width and hand each to the emitter.
 *
 * Breaks at spaces. A single word too long for the screen is broken at the
 * character that overflows, because the alternative is dropping it.
 *
 * @param out_first  first wrapped line to draw; earlier ones are counted only.
 * @param produced   running total of wrapped lines, advanced by this call.
 */
static void flipso_text_wrap(
    Canvas* canvas,
    const char* src,
    size_t len,
    bool bold,
    uint16_t width,
    uint16_t out_first,
    uint16_t out_rows,
    uint16_t* produced) {
    char buf[FLIPSO_TEXT_LINE_MAX];
    size_t fill = 0;
    uint16_t count = 0;

    /* Measuring is done against the font the line will actually be drawn in,
     * so a bold header wraps where a bold header breaks. */
    if(canvas) canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);

    /* Flush the assembled line, drawing it if it lands inside the window. */
#define FLIPSO_TEXT_FLUSH()                                                        \
    do {                                                                           \
        buf[fill] = '\0';                                                          \
        uint16_t line_no = *produced + count;                                      \
        bool visible = line_no >= out_first && line_no < out_first + out_rows;      \
        flipso_text_emit(visible ? canvas : NULL, buf, (uint16_t)(line_no - out_first), bold); \
        count++;                                                                   \
        fill = 0;                                                                  \
    } while(0)

    size_t i = 0;
    while(i < len) {
        /* Take the next word and the run of spaces in front of it. */
        size_t word_start = i;
        while(i < len && src[i] != ' ') i++;
        size_t word_len = i - word_start;
        while(i < len && src[i] == ' ') i++;

        if(word_len == 0) continue;

        /* A word that cannot fit an empty line is broken mid-word; nothing
         * else can be done with it, and dropping it would hide data. */
        if(word_len >= sizeof(buf)) word_len = sizeof(buf) - 1;

        size_t sep = fill ? 1 : 0;
        if(fill + sep + word_len < sizeof(buf)) {
            if(sep) buf[fill] = ' ';
            memcpy(buf + fill + sep, src + word_start, word_len);
            buf[fill + sep + word_len] = '\0';
        }

        bool fits = fill + sep + word_len < sizeof(buf) &&
                    (!canvas || canvas_string_width(canvas, buf) <= width);

        if(fits) {
            fill += sep + word_len;
            continue;
        }

        /* Does not fit alongside what is already there: start a new line. */
        if(fill) FLIPSO_TEXT_FLUSH();
        memcpy(buf, src + word_start, word_len);
        fill = word_len;
        buf[fill] = '\0';

        /* On its own line and still too wide: shed characters until it fits,
         * emit that, and carry the rest round again. */
        while(canvas && fill > 1 && canvas_string_width(canvas, buf) > width) {
            size_t keep = fill;
            while(keep > 1) {
                keep--;
                buf[keep] = '\0';
                if(canvas_string_width(canvas, buf) <= width) break;
            }
            size_t rest = fill - keep;
            FLIPSO_TEXT_FLUSH();
            memmove(buf, src + word_start + keep, rest);
            fill = rest;
            buf[fill] = '\0';
            word_start += keep;
            /* The font is per line, and flushing does not change it. */
            if(canvas) canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);
        }
    }

    /* A source line with no words is still a line: blank ones are the spacing
     * between the sections the scenes build. */
    if(fill || count == 0) FLIPSO_TEXT_FLUSH();

#undef FLIPSO_TEXT_FLUSH

    *produced += count;
}

static void flipso_text_view_draw(Canvas* canvas, void* model) {
    FlipsoTextModel* m = model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const char* text = m->text ? furi_string_get_cstr(m->text) : "";
    uint16_t rows = flipso_text_rows();
    uint16_t produced = 0;

    const char* line = text;
    while(true) {
        const char* nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);

        /* "\e#" turns the rest of the source line into a bold header, which is
         * the only markup the scenes use. */
        bool bold = len >= 2 && line[0] == '\e' && line[1] == '#';
        const char* body = bold ? line + 2 : line;
        size_t body_len = bold ? len - 2 : len;

        flipso_text_wrap(
            canvas, body, body_len, bold, FLIPSO_TEXT_RIGHT - FLIPSO_TEXT_X, m->scroll, rows,
            &produced);

        if(!nl) break;
        line = nl + 1;
    }

    /* The wrapped total is only knowable once the canvas has measured it, so
     * the scrollbar and the input clamp both use what this pass counted. */
    m->lines = produced;

    if(produced > rows) {
        elements_scrollbar_pos(
            canvas, FLIPSO_TEXT_SCREEN_W, 0, FLIPSO_TEXT_SCREEN_H, m->scroll,
            (uint16_t)(produced - rows + 1));
    }
}

/** Scroll by @p delta lines, stopping at the ends. */
static bool flipso_text_step(FlipsoTextView* instance, int16_t delta) {
    bool moved = false;
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            uint16_t rows = flipso_text_rows();
            /* Nothing to scroll until a draw has counted the lines. */
            uint16_t max = model->lines > rows ? (uint16_t)(model->lines - rows) : 0;
            int32_t next = (int32_t)model->scroll + delta;
            if(next < 0) next = 0;
            if(next > max) next = max;
            moved = (uint16_t)next != model->scroll;
            model->scroll = (uint16_t)next;
        },
        true);
    return moved;
}

static bool flipso_text_view_input(InputEvent* event, void* context) {
    FlipsoTextView* instance = context;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyUp) {
        flipso_text_step(instance, -1);
        return true;
    }
    if(event->key == InputKeyDown) {
        flipso_text_step(instance, 1);
        return true;
    }
    /* Back belongs to the scene manager, and OK does nothing here. */
    return false;
}

FlipsoTextView* flipso_text_view_alloc(void) {
    FlipsoTextView* instance = malloc(sizeof(FlipsoTextView));
    memset(instance, 0, sizeof(FlipsoTextView));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FlipsoTextModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, flipso_text_view_draw);
    view_set_input_callback(instance->view, flipso_text_view_input);

    with_view_model(
        instance->view, FlipsoTextModel * model, { model->text = furi_string_alloc(); }, false);
    return instance;
}

void flipso_text_view_free(FlipsoTextView* instance) {
    furi_assert(instance);
    /* The model owns its FuriString, so it has to go before the view does. */
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            furi_string_free(model->text);
            model->text = NULL;
        },
        false);
    view_free(instance->view);
    free(instance);
}

View* flipso_text_view_get_view(FlipsoTextView* instance) {
    furi_assert(instance);
    return instance->view;
}

void flipso_text_view_set_text(FlipsoTextView* instance, const char* text) {
    furi_assert(instance);
    furi_assert(text);
    with_view_model(
        instance->view,
        FlipsoTextModel * model,
        {
            furi_string_set_str(model->text, text);
            model->scroll = 0;
            model->lines = 0;
        },
        true);
}
